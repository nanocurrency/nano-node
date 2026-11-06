#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/election_status.hpp>
#include <nano/secure/common.hpp>

#include <boost/unordered/unordered_flat_map.hpp>

#include <vector>

namespace nano
{
/**
 * The most recently confirmed elections, found by root or by winner hash.
 * Entries fill a ring of `max_size` slots, where a confirmation past the limit takes the oldest slot, and two flat maps lead from a root or a hash to its slot.
 */
class recently_confirmed_cache final
{
public:
	explicit recently_confirmed_cache (std::size_t max_size);

	void put (nano::qualified_root const &, nano::block_hash const &, nano::election_status const &);
	void erase (nano::block_hash const &);
	void clear ();

	std::size_t size () const;
	bool contains (nano::qualified_root const &) const;
	bool contains (nano::block_hash const &) const;

	struct latency_stats
	{
		uint32_t p50{ 0 };
		uint32_t p90{ 0 };
		uint32_t p99{ 0 };
	};
	latency_stats latency_percentiles () const;

	nano::container_info container_info () const;

private:
	struct entry
	{
		nano::qualified_root root;
		nano::block_hash hash;
		nano::election_status status;
		bool live{ false }; // Cleared by erase (), the slot stays taken until it is reused
	};

	// Takes the entry in the slot out of both maps
	void release (std::size_t slot);

	std::vector<entry> slots; // Grows to `max_size`, then the oldest slot is reused
	std::size_t next{ 0 }; // Slot the next entry takes once the ring is full
	std::size_t live{ 0 }; // Entries that were not erased
	boost::unordered_flat_map<nano::qualified_root, std::size_t> by_root;
	boost::unordered_flat_map<nano::block_hash, std::size_t> by_hash;

	std::size_t const max_size;
	mutable nano::mutex mutex;
};
}
