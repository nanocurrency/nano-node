#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
/*
 * Topo blocks engine: tracks discovered-but-missing blocks, drives random-block fetching, and
 * releases fetched blocks to the block processor in topological order.
 *
 * Entries are ordered by topo_key so the submit cursor can release a contiguous, dependency-safe
 * prefix; a hashed index on the block hash matches fetch responses back to their entry. A block
 * that cannot be fetched after `max_fetch_attempts` is demoted to a tolerated gap (skip) so it
 * never wedges the submit cursor. Pure state, guarded externally by the bootstrap_context mutex.
 */
class topo_blocks
{
public:
	topo_blocks (topo_scan_config const &, nano::stats &, nano::logger &);

	struct request
	{
		std::deque<nano::block_hash> hashes;
		std::vector<nano::account> exclude;
	};

	// Record discovered topo entries that are missing from our ledger (insert-if-absent; re-arms tolerated gaps)
	void add (std::deque<nano::topo_key> const & entries);

	// Reserve a batch description to fetch next, with peers already sampled by those entries excluded
	std::optional<request> next (std::size_t max, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// Register an issued fetch request. Returns false if all requested entries became stale before dispatch
	bool dispatch (request const &, id_t id, nano::account node_id, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ());

	// The peer pool is exhausted for this request; clear sampled peers so the entries can begin a fresh sweep
	void exhausted (request const &);

	// Match fetched blocks to their entries and mark them ready for submission
	void process (id_t id, std::deque<std::shared_ptr<nano::block>> const & blocks);

	// Drop a reservation whose request failed or timed out
	void cancel (id_t id);

	// True if the lowest entry is ready to release (fetched or a tolerated gap)
	bool has_submittable () const;

	// The next contiguous topologically-ordered batch of fetched blocks; removes released entries
	std::deque<std::shared_ptr<nano::block>> next_submit (std::size_t max);

	// Number of entries still awaiting fetch
	std::size_t pending_count () const;

	// Number of active fetch entries
	std::size_t inflight_count () const;

	// Total entries held in the buffer: awaiting fetch plus fetched awaiting submit (the scan back-pressure signal)
	std::size_t total_count () const;

	void reset ();

	nano::container_info container_info () const;

private: // Dependencies
	topo_scan_config const & config;
	nano::stats & stats;
	nano::logger & logger;

private:
	enum class entry_state
	{
		pending, // Missing from the ledger, awaiting fetch
		fetched, // Block in hand, awaiting in-order submission
		skipped, // Could not be fetched; a tolerated gap that does not block the submit cursor
	};

	struct entry
	{
		nano::topo_key key;
		std::shared_ptr<nano::block> block; // Set once fetched
		entry_state state{ entry_state::pending };
		unsigned attempts{ 0 };
		std::chrono::steady_clock::time_point last_requested{};
		std::unordered_set<nano::account> sampled;

		nano::block_hash hash () const
		{
			return key.hash;
		}
	};

	// clang-format off
	class tag_key {};
	class tag_hash {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::ordered_unique<mi::tag<tag_key>,
			mi::member<entry, nano::topo_key, &entry::key>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<entry, nano::block_hash, &entry::hash>>
	>>;
	// clang-format on

	ordered_entries entries;

	std::size_t pending_counter{ 0 }; // Count of entries awaiting fetch

	struct reservation
	{
		id_t id; // Request ID
		nano::block_hash hash; // Requested block
	};

	// clang-format off
	class tag_inflight_id {};
	class tag_inflight_hash {};

	using inflight_reservations = boost::multi_index_container<reservation,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<tag_inflight_id>,
			mi::member<reservation, id_t, &reservation::id>>,
		mi::hashed_non_unique<mi::tag<tag_inflight_hash>,
			mi::member<reservation, nano::block_hash, &reservation::hash>>>>;
	// clang-format on

	inflight_reservations inflight;
};
}
