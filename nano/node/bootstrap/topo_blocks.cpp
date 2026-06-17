#include <nano/lib/blocks.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/bootstrap/topo_blocks.hpp>

#include <algorithm>

namespace nano::bootstrap
{
topo_blocks::topo_blocks (topo_scan_config const & config_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	stats{ stats_a },
	logger{ logger_a }
{
}

void topo_blocks::add (std::deque<nano::topo_key> const & new_entries)
{
	auto & by_key = entries.get<tag_key> ();
	for (auto const & key : new_entries)
	{
		auto it = by_key.find (key);
		if (it == by_key.end ())
		{
			by_key.insert (entry{ .key = key });
			++pending_counter;
			stats.inc (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::pending);
		}
		else if (it->state == entry_state::skipped)
		{
			// Repair re-discovery: give a previously abandoned gap another chance
			by_key.modify (it, [] (entry & e) {
				e.state = entry_state::pending;
				e.attempts = 0;
				e.last_requested = {};
				e.sampled.clear ();
			});
			++pending_counter;
		}
	}
}

std::optional<topo_blocks::request> topo_blocks::next (std::size_t max, std::chrono::steady_clock::time_point now)
{
	request result;
	auto & by_key = entries.get<tag_key> ();
	auto const & by_inflight_hash = inflight.get<tag_inflight_hash> ();

	auto add_excludes = [&result] (std::unordered_set<nano::account> const & sampled) {
		for (auto const & node_id : sampled)
		{
			if (std::find (result.exclude.begin (), result.exclude.end (), node_id) == result.exclude.end ())
			{
				result.exclude.push_back (node_id);
			}
		}
	};

	for (auto it = by_key.begin (); it != by_key.end () && result.hashes.size () < max; ++it)
	{
		if (it->state != entry_state::pending)
		{
			continue;
		}
		if (it->attempts >= config.max_fetch_attempts)
		{
			// Wait for outstanding responses before turning the entry into a tolerated gap
			if (by_inflight_hash.contains (it->key.hash))
			{
				continue;
			}
			// Abandon as a tolerated gap so it never blocks the submit cursor
			logger.debug (nano::log::type::bootstrap, "Topology bootstrap skipped random fetch entry after {} attempts: topo_height {} hash {}", it->attempts, it->key.topo_height, it->key.hash);
			by_key.modify (it, [] (entry & e) { e.state = entry_state::skipped; });
			--pending_counter;
			stats.inc (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip);
			continue;
		}
		bool const ready = it->last_requested == std::chrono::steady_clock::time_point{} || (now - it->last_requested) >= config.fetch_cooldown;
		if (ready)
		{
			result.hashes.push_back (it->key.hash);
			add_excludes (it->sampled);
		}
	}

	if (result.hashes.empty ())
	{
		return std::nullopt;
	}
	return result;
}

bool topo_blocks::dispatch (request const & req, id_t id, nano::account node_id, std::chrono::steady_clock::time_point now)
{
	auto & by_hash = entries.get<tag_hash> ();
	std::deque<nano::block_hash> dispatched;
	for (auto const & hash : req.hashes)
	{
		auto it = by_hash.find (hash);
		if (it != by_hash.end () && it->state == entry_state::pending && it->sampled.find (node_id) == it->sampled.end ())
		{
			by_hash.modify (it, [node_id, now] (entry & e) {
				e.sampled.insert (node_id);
				e.last_requested = now;
				e.attempts += 1;
			});
			dispatched.push_back (hash);
		}
	}

	if (dispatched.empty ())
	{
		return false;
	}

	for (auto const & hash : dispatched)
	{
		inflight.insert ({ id, hash });
	}
	return true;
}

void topo_blocks::exhausted (request const & req)
{
	auto & by_hash = entries.get<tag_hash> ();
	for (auto const & hash : req.hashes)
	{
		auto it = by_hash.find (hash);
		if (it != by_hash.end () && it->state == entry_state::pending)
		{
			by_hash.modify (it, [] (entry & e) {
				e.sampled.clear ();
				e.last_requested = {};
			});
		}
	}
}

void topo_blocks::process (id_t id, std::deque<std::shared_ptr<nano::block>> const & blocks)
{
	auto & by_inflight_id = inflight.get<tag_inflight_id> ();
	auto const [begin, end] = by_inflight_id.equal_range (id);
	if (begin == end)
	{
		return;
	}
	by_inflight_id.erase (begin, end);

	auto & by_hash = entries.get<tag_hash> ();
	for (auto const & block : blocks)
	{
		auto it = by_hash.find (block->hash ());
		if (it != by_hash.end () && it->state == entry_state::pending)
		{
			by_hash.modify (it, [&block] (entry & e) {
				e.state = entry_state::fetched;
				e.block = block;
			});
			--pending_counter;
			stats.inc (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::fetched);
		}
	}
}

void topo_blocks::cancel (id_t id)
{
	auto & by_inflight_id = inflight.get<tag_inflight_id> ();
	auto const [begin, end] = by_inflight_id.equal_range (id);
	if (begin == end)
	{
		return;
	}

	auto & by_hash = entries.get<tag_hash> ();
	for (auto it = begin; it != end; ++it)
	{
		auto entry_it = by_hash.find (it->hash);
		if (entry_it != by_hash.end () && entry_it->state == entry_state::pending)
		{
			// Make immediately eligible again, but keep the sampled peer excluded until the peer pool is exhausted.
			by_hash.modify (entry_it, [] (entry & e) { e.last_requested = {}; });
		}
	}
	by_inflight_id.erase (begin, end);
}

bool topo_blocks::has_submittable () const
{
	auto const & by_key = entries.get<tag_key> ();
	if (by_key.empty ())
	{
		return false;
	}
	auto const & front = *by_key.begin ();
	return front.state == entry_state::fetched || front.state == entry_state::skipped;
}

std::deque<std::shared_ptr<nano::block>> topo_blocks::next_submit (std::size_t max)
{
	std::deque<std::shared_ptr<nano::block>> batch;
	auto & by_key = entries.get<tag_key> ();

	for (auto it = by_key.begin (); it != by_key.end () && batch.size () < max;)
	{
		if (it->state == entry_state::pending)
		{
			break; // Real gap: cannot release anything past it in topological order
		}
		if (it->state == entry_state::fetched)
		{
			batch.push_back (it->block);
			stats.inc (nano::stat::type::bootstrap_topo_submit, nano::stat::detail::submitted);
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_topo_submit, nano::stat::detail::gap);
		}
		// Both fetched and tolerated-gap entries are consumed as the cursor advances
		it = by_key.erase (it);
	}

	return batch;
}

std::size_t topo_blocks::pending_count () const
{
	return pending_counter;
}

std::size_t topo_blocks::inflight_count () const
{
	return inflight.size ();
}

std::size_t topo_blocks::total_count () const
{
	return entries.size ();
}

void topo_blocks::reset ()
{
	entries.clear ();
	inflight.clear ();
	pending_counter = 0;
}

nano::container_info topo_blocks::container_info () const
{
	nano::container_info info;
	info.put ("total", entries.size ());
	info.put ("pending", pending_count ());
	info.put ("inflight", inflight_count ());
	return info;
}
}
