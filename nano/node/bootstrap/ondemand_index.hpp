#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace mi = boost::multi_index;

namespace nano::bootstrap
{
/*
 * Blocks that are known to exist but are missing from the ledger, each with the state of its retrieval.
 * A block first waits for a delay in case it arrives on its own, is then requested from one peer at a time,
 * and once fetched stays tracked until the ledger reports what became of it.
 * Pure state, guarded externally by the bootstrap_context mutex.
 */
class ondemand_index
{
public:
	explicit ondemand_index (ondemand_config const &);

	// What makes a block wanted, from the weakest evidence to the strongest
	enum class reason
	{
		dependency, // Another block cannot be applied without it
		votes, // Representatives voted for it
	};

	enum class insert_result
	{
		inserted, // Newly tracked
		duplicate, // Already tracked
		replaced, // Newly tracked, the oldest block with the weakest reason made room for it
		revived, // Already tracked but abandoned, it starts over now that there is a stronger reason to want it
		overfill, // Refused, every tracked block has a stronger reason
	};

	enum class fail_result
	{
		retry, // Due for another request once its cooldown has passed
		abandoned, // Out of attempts, stays tracked so that it is not requested again
		stale, // The block was not waiting on that request
	};

	struct request
	{
		nano::block_hash hash;
		ondemand_index::reason reason; // What makes the block wanted
		std::vector<nano::account> exclude; // Peers that already failed to deliver the block
	};

	struct failure
	{
		nano::block_hash hash;
		fail_result result;
	};

	// Starts tracking a block, due for its first request once the delay has passed
	insert_result insert (nano::block_hash const &, reason, std::chrono::steady_clock::time_point now);

	// The most urgent block due for a request among those wanted for `weakest` or a stronger reason: the strongest reason first, then the one due for longest
	std::optional<request> next (reason weakest, std::chrono::steady_clock::time_point now) const;

	// Records a request sent to a peer, false if the block no longer waits for one
	bool dispatch (nano::block_hash const &, id_t id, nano::account const & peer, std::chrono::steady_clock::time_point now);

	// Lets the block be requested from peers that already failed to deliver it, for when no other peer is left
	void exhausted (nano::block_hash const &);

	// The request came back without a usable block
	fail_result fail (nano::block_hash const &, id_t id, std::chrono::steady_clock::time_point now);

	// Fails every request that got no answer in time and reports what became of each block
	std::vector<failure> timeout (std::chrono::steady_clock::time_point now);

	// The block arrived and is handed to the block processor, false if it is not tracked or was handed over already
	bool submit (nano::block_hash const &);

	// The block processor could not use the copy it was handed
	fail_result reject (nano::block_hash const &, std::chrono::steady_clock::time_point now);

	// The handed over block cannot enter the ledger for now, it stays tracked only so that it is not requested again
	bool hold (nano::block_hash const &);

	// Stops tracking a block, returns when it became wanted if it was tracked
	std::optional<std::chrono::steady_clock::time_point> erase (nano::block_hash const &);

	// Forgets blocks tracked for longer than the maximum age, returns how many
	std::size_t expire (std::chrono::steady_clock::time_point now);

	void clear ();

	// What makes a tracked block wanted
	std::optional<reason> find_reason (nano::block_hash const &) const;

	bool contains (nano::block_hash const &) const;
	std::size_t size () const;

	nano::container_info container_info () const;

private: // Dependencies
	ondemand_config const & config;

private:
	enum class entry_state
	{
		waiting, // Due for a request at `due`
		requested, // Asked a peer, which has until `due` to answer
		submitted, // Handed to the block processor, its result is pending
		held, // Fetched, but the ledger cannot take it for now
		abandoned, // Out of attempts, a copy that still arrives is taken
	};

	struct entry
	{
		nano::block_hash hash;
		ondemand_index::reason reason;
		std::chrono::steady_clock::time_point arrival; // When the block became wanted
		entry_state state{ entry_state::waiting };
		std::chrono::steady_clock::time_point due; // Meaning depends on the state
		id_t id{ 0 }; // Request the block waits on while requested
		unsigned attempts{ 0 }; // Requests sent so far

		// Peers asked so far
		// Most blocks are delivered by the first peer, so the list rarely grows past the entry itself
		boost::container::small_vector<nano::account, 1> tried;
	};

	// Puts a block whose request or copy failed back in line, or gives up on it
	fail_result retry_or_abandon (entry const &, std::chrono::steady_clock::time_point now);

	// clang-format off
	class tag_hash {};
	class tag_schedule {};
	class tag_age {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_schedule>,
			mi::composite_key<entry,
				mi::member<entry, entry_state, &entry::state>,
				mi::member<entry, ondemand_index::reason, &entry::reason>,
				mi::member<entry, std::chrono::steady_clock::time_point, &entry::due>>>,
		mi::ordered_non_unique<mi::tag<tag_age>,
			mi::composite_key<entry,
				mi::member<entry, ondemand_index::reason, &entry::reason>,
				mi::member<entry, std::chrono::steady_clock::time_point, &entry::arrival>>>
	>>;
	// clang-format on

	ordered_entries entries;
};

nano::stat::detail to_stat_detail (ondemand_index::insert_result);
nano::stat::detail to_stat_detail (ondemand_index::fail_result);
}
