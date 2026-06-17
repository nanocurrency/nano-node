#pragma once

#include <nano/lib/assert.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
// Stable identity of a scan head: 0 is the spearhead, 1..N are the repair heads
using head_index = unsigned;

/*
 * Topo scan engine: walks the peers' topology index across multiple heads.
 *
 * Every head samples each cursor across `consideration` distinct peers, aggregates their replies, keeps the
 * smallest `candidates`, and advances past them (sampling more than one peer guards against over-advancing on
 * a single lagging peer). The heads differ only in how they advance: the spearhead pushes the discovery
 * frontier forward into unseen topo-height space, while the repair heads each own a frozen band of the
 * discovered range [1, frontier] and re-scan it for gaps, disarming when a sweep reaches the band's end.
 *
 * A round is issued in two steps: next () reserves the oldest due head and returns how many distinct peers
 * to fan out to, then dispatch () registers each request as the strategy acquires a peer for it. Heads are
 * ordered by last-query time so the scan loop always picks up the one queried longest ago.
 *
 * Pure state, guarded externally by the bootstrap_context mutex (mirrors frontier_scan_index).
 */
class topo_scan
{
public:
	topo_scan (topo_scan_config const &, nano::stats &);

	// A page scan round to issue: walk the peer's topo index from `start`, up to `count` entries, across up to
	// `fanout` distinct peers, none of which appear in `exclude` (peers already sampled for this cursor)
	struct request
	{
		head_index head{ 0 };
		nano::topo_key start;
		std::size_t count{ 0 };
		unsigned fanout{ 0 };
		std::vector<nano::account> exclude;
	};

	// A retired page handed back for pre-check, tagged with the head that produced it (0 is the spearhead)
	struct page
	{
		head_index head{ 0 }; // Stable head identity: 0 is the spearhead, 1..N are the repair heads
		unsigned redundancy{ 0 }; // Number of distinct peer replies that completed the page
		std::deque<nano::topo_key> entries;
	};

	// Per-head-class admission gates (back-pressure): a class issues a round only while its gate is open
	struct head_gates
	{
		bool include_spearhead{ false };
		bool include_repair{ false };
	};

	// Re-anchor the spearhead and frontier to at least this topology position
	void orient (nano::topo_key latest);

	// Grow the repair head count to match the current frontier (frontier is monotonic, so heads are only added)
	void reconcile_heads ();

	// Reserve the oldest due head and return the round to issue, or nullopt if none is due.
	// `gates` back-pressures each head class independently (an open gate lets that class run)
	std::optional<request> next (head_gates gates, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// Register one issued request of a round (called once per acquired peer). Returns false if the head
	// advanced under us since next () (a stale round; the caller drops it)
	bool dispatch (head_index head, nano::topo_key start, id_t id, nano::account node_id);

	// Record that the round could not reach consideration_count distinct peers (the pool is exhausted),
	// lowering the head's advance bar. May complete the round, emitting a page to the sink
	void exhausted (head_index head, nano::topo_key start);

	// Apply a response page to the head that issued `id`. May complete the round, emitting a page to the sink
	void process (id_t id, std::deque<nano::topo_key> const & entries);

	// Drop a reservation whose request failed or timed out. May complete an exhausted round, emitting a page to the sink
	void cancel (id_t id);

	void reset ();

	// True while any head is exhausted yet cannot reach its redundancy floor (too few peers to complete a page scan)
	bool starved () const;

	nano::container_info container_info () const;

public:
	// Retirement sink: invoked with each completed page
	std::function<void (page)> sink = [] (page) { debug_assert (false, "topo_scan sink not wired"); };

private: // Dependencies
	topo_scan_config const & config;
	nano::stats & stats;

private:
	enum class head_type
	{
		spearhead, // Advances the discovery frontier forward into unseen topo-height space
		repair, // Re-scans a band of the discovered range to fill gaps
	};

	// A half-open range [lo, hi) of topo positions
	struct band
	{
		nano::topo_key lo;
		nano::topo_key hi;
	};

	struct head
	{
		head_type type{ head_type::repair };
		head_index id{ 0 }; // Stable identity: 0 is the spearhead, 1..N are the repair heads
		unsigned consideration{ 1 }; // Distinct peers to sample for a cursor before advancing
		nano::topo_key cursor{}; // Start position for this head's next request
		band range{}; // Repair only: the band frozen for the current sweep; hi is zero until first frozen
		std::map<nano::topo_key, unsigned> candidates; // Entries aggregated across this cursor's replies, with per-key support count
		std::unordered_set<nano::account> sampled; // Distinct peers sampled for the current cursor
		unsigned requests{ 0 }; // Distinct samples issued for the current cursor (mirrors sampled.size ())
		unsigned completed{ 0 }; // Replies received for the current cursor
		bool exhausted{ false }; // No more distinct peers available for the current cursor
		std::chrono::steady_clock::time_point timestamp{}; // Last time this head was queried
		std::size_t processed{ 0 };

		bool is_spearhead () const
		{
			return type == head_type::spearhead;
		}

		// Minimum distinct peer replies required to advance: ceil (consideration / 2)
		unsigned floor () const
		{
			return (consideration + 1) / 2;
		}

		// Replies that complete the round: the full consideration, or all the exhausted pool could supply
		unsigned target () const
		{
			return exhausted ? requests : consideration;
		}

		// Begin a fresh round at `next`: forget all samples and aggregation, preserving pacing timestamp
		void restart (nano::topo_key next)
		{
			cursor = next;
			candidates.clear ();
			sampled.clear ();
			requests = 0;
			completed = 0;
			exhausted = false;
		}

		// Begin a fresh sweep over `b`: adopt it as the frozen band and rewind the cursor to its start
		void start_sweep (band const & b)
		{
			range = b;
			restart (b.lo);
		}

		// A repair head is armed once it holds a band to sweep, and disarmed when that band is exhausted
		bool armed () const
		{
			return range.hi != nano::topo_key{};
		}

		// Mark the band exhausted; next () will re-arm the head with a fresh band
		void disarm ()
		{
			range = {};
		}
	};

	// Tracks the head, peer, and cursor an in-flight request was issued for, so its reply or timeout can be
	// attributed and discarded if the head has since advanced past `start`
	struct reservation
	{
		head_index head{ 0 };
		nano::account node_id{ 0 };
		nano::topo_key start{};
	};

	// clang-format off
	class tag_id {};
	class tag_timestamp {};

	using ordered_heads = boost::multi_index_container<head,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<head, head_index, &head::id>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<head, std::chrono::steady_clock::time_point, &head::timestamp>>
	>>;
	// clang-format on

	// Advance the head once its round has gathered enough distinct replies, emitting the retired page to the sink.
	// The spearhead moves the frontier forward; a repair head moves within its band and disarms at the band's end.
	void maybe_advance (head &);

	// The band owned by zero-based repair head `repair_index`, computed from the current frontier and live repair head count
	band repair_band (head_index repair_index) const;

	// Recent lookback band swept by repair head 1 so fast-forwards can be healed promptly
	band trailing_repair_band () const;

	// Number of repair heads the current frontier calls for: ceil (frontier / repair_band_height), clamped
	unsigned desired_repair_heads () const;

	ordered_heads heads;
	nano::topo_key frontier{}; // Highest discovered topo position
	std::unordered_map<id_t, reservation> inflight; // request id -> reservation
};
}
