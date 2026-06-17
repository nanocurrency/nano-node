#include <nano/lib/container_info.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/topo_scan.hpp>

#include <algorithm>
#include <limits>

namespace nano::bootstrap
{
topo_scan::topo_scan (topo_scan_config const & config_a, nano::stats & stats_a) :
	config{ config_a },
	stats{ stats_a }
{
	reset ();
}

void topo_scan::reset ()
{
	heads.clear ();
	inflight.clear ();
	frontier = {};

	// Head 0 is the spearhead (anchored to our local tip by orient)
	heads.insert (head{ .type = head_type::spearhead, .id = 0, .consideration = config.consideration_count });

	// Seed the floor of repair heads; the count grows with the frontier height from here
	reconcile_heads ();
}

void topo_scan::orient (nano::topo_key latest)
{
	auto const target = std::max (frontier, latest);
	frontier = target;
	inflight.clear ();

	auto & by_id = heads.get<tag_id> ();
	for (auto it = by_id.begin (); it != by_id.end (); ++it)
	{
		by_id.modify (it, [target] (head & h) {
			if (h.is_spearhead ())
			{
				h.restart (target);
			}
			else
			{
				h.disarm ();
				h.restart ({});
			}
			h.timestamp = {};
		});
	}

	reconcile_heads (); // a node starting from a large local ledger should come up at the right head count
}

unsigned topo_scan::desired_repair_heads () const
{
	uint64_t const band = config.repair_band_height > 0 ? config.repair_band_height : 1;
	uint64_t const want = (frontier.topo_height + band - 1) / band; // ceil: one repair head per band of height
	return static_cast<unsigned> (std::clamp<uint64_t> (want, config.min_repair_heads, config.max_repair_heads));
}

void topo_scan::reconcile_heads ()
{
	auto & by_id = heads.get<tag_id> ();
	debug_assert (by_id.size () > 0);
	unsigned const current = static_cast<unsigned> (by_id.size ()) - 1; // minus the spearhead
	unsigned const desired = desired_repair_heads ();

	// The frontier never shrinks within a session, so the count only ever grows: append the missing repair heads.
	// A new head carries a zero timestamp, so next () picks it up immediately and arms it onto a fresh band.
	for (unsigned i = current; i < desired; ++i)
	{
		heads.insert (head{ .type = head_type::repair, .id = i + 1, .consideration = config.repair_consideration });
		stats.inc (nano::stat::type::bootstrap_topo_scan, nano::stat::detail::grow);
	}
}

topo_scan::band topo_scan::trailing_repair_band () const
{
	auto const stride = config.redundant_skip_stride;
	uint64_t const lo_height = frontier.topo_height > stride ? frontier.topo_height - stride : 1;
	uint64_t const hi_height = frontier.topo_height < std::numeric_limits<uint64_t>::max () ? frontier.topo_height + 1 : frontier.topo_height;
	return { nano::topo_key{ lo_height, 0 }, nano::topo_key{ hi_height, 0 } };
}

topo_scan::band topo_scan::repair_band (head_index repair_index) const
{
	if (repair_index == 0)
	{
		return trailing_repair_band ();
	}

	// The remaining repair heads divide the full discovered range; head 1 is reserved for the recent lookback band.
	debug_assert (heads.size () > 0);
	uint64_t const n = std::max<uint64_t> (1, heads.size () > 2 ? heads.size () - 2 : 1);
	uint64_t const broad_index = repair_index - 1;
	uint64_t lo_height = 1 + (static_cast<uint64_t> (broad_index) * frontier.topo_height) / n;
	uint64_t hi_height = 1 + (static_cast<uint64_t> (broad_index + 1) * frontier.topo_height) / n;
	return { nano::topo_key{ lo_height, 0 }, nano::topo_key{ hi_height, 0 } };
}

std::optional<topo_scan::request> topo_scan::next (head_gates gates, std::chrono::steady_clock::time_point now)
{
	auto const cutoff = now - config.cooldown;

	// A head is due while an active round still wants distinct samples, or once its cooldown elapses. The default
	// zero timestamp is older than any cutoff, so fresh heads and progress-cleared heads fire immediately.
	// Each head class is additionally hard-gated by its own back-pressure flag.
	auto is_due = [&] (head const & h) {
		bool const open = h.is_spearhead () ? gates.include_spearhead : gates.include_repair;
		bool const want_more = !h.exhausted && h.requests > 0 && h.requests < h.consideration;
		bool const cooldown_expired = h.timestamp < cutoff;
		return open && (want_more || cooldown_expired);
	};

	// Arm and stamp a due head, returning the round to issue. Cooldown can re-poll a full or exhausted round
	// without discarding replies already gathered, so fanout saturates to at least one.
	auto reserve = [&] (head & h) {
		// The single place a repair band is set: arm any unarmed head (its first sweep, or after it wrapped)
		if (!h.is_spearhead () && !h.armed ())
		{
			auto const repair_index = h.id - 1;
			h.start_sweep (repair_band (repair_index));
		}
		unsigned const fanout = h.requests < h.consideration ? h.consideration - h.requests : 1;
		h.exhausted = false;
		h.timestamp = now;
		std::vector<nano::account> exclude (h.sampled.begin (), h.sampled.end ()); // peers already sampled this cursor
		return request{ h.id, h.cursor, nano::messages::asc_pull_ack::topo_index_payload::max_entries, fanout, std::move (exclude) };
	};

	// Issue the round for a due head: count it, reserve it under its own index, and return the request
	auto issue = [&] (auto & index, auto it, nano::stat::detail detail) {
		stats.inc (nano::stat::type::bootstrap_topo_scan, detail);
		request req;
		index.modify (it, [&] (head & h) { req = reserve (h); });
		return req;
	};

	// Always prefer the spearhead; only when it is in progress, at capacity, or back-pressured do repair heads run
	auto & by_id = heads.get<tag_id> ();
	if (auto it = by_id.find (0); it != by_id.end ())
	{
		if (is_due (*it))
		{
			return issue (by_id, it, nano::stat::detail::next_spearhead);
		}
	}

	// Fall back to repair heads, the one queried longest ago first
	auto & by_timestamp = heads.get<tag_timestamp> ();
	for (auto it = by_timestamp.begin (); it != by_timestamp.end (); ++it)
	{
		if (!it->is_spearhead () && is_due (*it))
		{
			return issue (by_timestamp, it, nano::stat::detail::next_repair);
		}
	}

	stats.inc (nano::stat::type::bootstrap_topo_scan, nano::stat::detail::next_none);
	return std::nullopt;
}

bool topo_scan::dispatch (head_index head, nano::topo_key start, id_t id, nano::account node_id)
{
	auto & by_id = heads.get<tag_id> ();
	auto it = by_id.find (head);
	if (it == by_id.end () || it->cursor != start || it->sampled.find (node_id) != it->sampled.end ())
	{
		return false; // gone, or advanced under us since next () → stale round
	}

	inflight[id] = reservation{ head, node_id, start };
	by_id.modify (it, [node_id] (auto & h) {
		auto inserted = h.sampled.insert (node_id).second;
		debug_assert (inserted);
		h.requests += 1;
	});
	return true;
}

void topo_scan::exhausted (head_index head, nano::topo_key start)
{
	auto & by_id = heads.get<tag_id> ();
	auto it = by_id.find (head);
	if (it == by_id.end () || it->cursor != start)
	{
		return; // gone, or advanced under us
	}

	by_id.modify (it, [this] (auto & h) {
		h.exhausted = true;
		// Lowering the bar to the peers we actually reached may complete the round right away (emitted to the sink)
		maybe_advance (h);
	});
}

void topo_scan::process (id_t id, std::deque<nano::topo_key> const & entries)
{
	// Resolve which head this response belongs to; a missing id means it was cancelled or already handled
	auto inflight_it = inflight.find (id);
	if (inflight_it == inflight.end ())
	{
		return;
	}
	auto const res = inflight_it->second;
	inflight.erase (inflight_it);

	auto & by_id = heads.get<tag_id> ();
	auto it = by_id.find (res.head);
	if (it == by_id.end ())
	{
		return;
	}

	by_id.modify (it, [this, &res, &entries] (head & h) {
		// Stale reply: the head advanced past where this request started, so it sampled a different position
		if (res.start != h.cursor)
		{
			return;
		}

		h.processed += entries.size ();
		h.completed += 1; // A distinct peer replied for the current cursor

		// Aggregate this reply's new entries, capping the map to the smallest `candidates`. Responses include the
		// cursor, so `candidates` is a trim limit for usable new entries, not the requested page size.
		for (auto const & entry : entries)
		{
			if (h.cursor < entry)
			{
				++h.candidates[entry];
			}
		}
		while (h.candidates.size () > config.candidates)
		{
			h.candidates.erase (std::prev (h.candidates.end ())); // Drop the largest, keep the smallest
		}

		maybe_advance (h); // emits the retired page to the sink if the round completed
	});
}

void topo_scan::maybe_advance (head & h)
{
	// Advance once the round is complete (every expected reply gathered) and at least the floor of distinct
	// peers agreed; below the floor the agreement is too thin to commit, so the head keeps re-polling.
	if (h.requests > 0 && h.completed >= h.target () && h.completed >= h.floor ())
	{
		if (h.candidates.empty ())
		{
			// Nothing new — tip idle (spearhead) or no gaps in the band right now (repair). Clear the round
			// while preserving timestamp, so the cooldown paces the next poll from this cursor.
			h.restart (h.cursor);
			return;
		}

		// Advance to the largest kept candidate with enough per-key support. Retire all discovered entries up to
		// that supported boundary, but never let a minority-only tail move the cursor past unseen keys.
		auto const floor = h.floor ();
		auto const furthest_it = std::find_if (h.candidates.rbegin (), h.candidates.rend (), [floor] (auto const & candidate) {
			return candidate.second >= floor;
		});
		if (furthest_it == h.candidates.rend ())
		{
			return;
		}
		auto const furthest = furthest_it->first;

		if (h.is_spearhead ())
		{
			frontier = std::max (frontier, furthest); // Push the discovery frontier forward
		}
		else if (furthest >= h.range.hi)
		{
			h.disarm (); // Reached the band end; next () re-arms a fresh band
		}

		std::deque<nano::topo_key> retire;
		for (auto it = h.candidates.begin (); it != h.candidates.end () && it->first <= furthest; ++it)
		{
			retire.push_back (it->first);
		}

		auto const redundancy = h.completed;

		h.restart (furthest);
		h.timestamp = {}; // Made progress: re-fire promptly (chase the frontier / sweep the band)

		// Emit the completed page to the sink
		sink (page{
		.head = h.id,
		.redundancy = redundancy,
		.entries = std::move (retire) });
	}
}

void topo_scan::cancel (id_t id)
{
	auto inflight_it = inflight.find (id);
	if (inflight_it == inflight.end ())
	{
		return;
	}
	auto const res = inflight_it->second;
	inflight.erase (inflight_it);

	auto & by_id = heads.get<tag_id> ();
	auto it = by_id.find (res.head);
	if (it == by_id.end ())
	{
		return;
	}

	by_id.modify (it, [this, &res] (head & h) {
		// Stale timeout: the head advanced past where this request started; it belongs to a finished round
		if (res.start != h.cursor)
		{
			return;
		}
		// Drop the failed peer and clear exhaustion so the freed slot is re-sampled at once (scan_one re-flags it if still dry), keeping replies already gathered
		if (h.sampled.erase (res.node_id) > 0)
		{
			h.requests -= 1;
			h.exhausted = false;
			h.timestamp = {};
		}
		maybe_advance (h); // emits the retired page to the sink if the round completed
	});
}

bool topo_scan::starved () const
{
	// A head is starved when it has exhausted the peer pool yet sampled fewer than its redundancy floor of
	// distinct peers — too few to ever complete a page scan, whether because they don't exist or timed out.
	return std::any_of (heads.begin (), heads.end (), [] (head const & h) {
		return h.exhausted && h.requests < h.floor ();
	});
}

nano::container_info topo_scan::container_info () const
{
	auto collect_heads = [&] () {
		nano::container_info info;
		for (auto const & h : heads)
		{
			info.put (std::to_string (h.id), h.cursor.topo_height);
		}
		info.put ("frontier", frontier.topo_height);
		return info;
	};

	auto collect_processed = [&] () {
		nano::container_info info;
		for (auto const & h : heads)
		{
			info.put (std::to_string (h.id), h.processed);
		}
		return info;
	};

	nano::container_info info;
	info.put ("inflight", inflight.size ());
	info.add ("heads", collect_heads ());
	info.add ("processed", collect_processed ());
	return info;
}
}
