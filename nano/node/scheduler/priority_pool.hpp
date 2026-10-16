#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

namespace mi = boost::multi_index;

namespace nano::scheduler
{
struct priority_entry
{
	std::shared_ptr<nano::block> block;
	nano::bucket_index bucket;
	nano::priority_timestamp priority;
};

/*
 * A global pool of priority blocks to be scheduled for election
 * Blocks are grouped by their bucket and ordered by their priority (lower priority value goes first)
 * The benefit of a shared pool is that unused bucket capacity could be utilized by active buckets
 * The pool has a maximum size, and new blocks are only added if there is space or if bucket size is below reserved capacity
 */
class priority_pool final
{
public:
	explicit priority_pool (size_t max_size, size_t bucket_reserved);

	/// Add a block to the pool with given bucket and priority
	bool push (std::shared_ptr<nano::block> const & block, nano::bucket_index bucket, nano::priority_timestamp priority);

	/// Peek or pop the next priority block (lower priority value goes first) for a given bucket
	std::optional<priority_entry> top (nano::bucket_index bucket) const;
	std::optional<priority_entry> pop (nano::bucket_index bucket);

	/// Get the best priority block for all non-empty buckets
	std::map<nano::bucket_index, priority_entry> top_all () const;

	bool erase (nano::block_hash const & hash);
	size_t erase_all (std::deque<nano::block_hash> const & hashes);

	/// Evict the worst priority block (highest priority value) from a given bucket
	nano::block_hash evict (nano::bucket_index bucket);

	bool contains (nano::block_hash const & hash) const;
	bool any (nano::bucket_index bucket) const;

	size_t size () const;
	size_t size (nano::bucket_index bucket) const;
	bool empty () const;
	bool empty (nano::bucket_index bucket) const;

	void clear ();

	nano::container_info container_info () const;

private:
	size_t const max_size;
	size_t const bucket_reserved;

	struct entry
	{
		nano::bucket_index bucket;
		nano::priority_timestamp priority;
		nano::block_hash hash;
		std::shared_ptr<nano::block> block;
	};

	// clang-format off
	class tag_bucket_priority {};
	class tag_hash {};
	class tag_priority {};

	using ordered_entries = boost::multi_index_container<entry,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_bucket_priority>,
			mi::composite_key<entry,
				mi::member<entry, nano::bucket_index, &entry::bucket>,
				mi::member<entry, nano::priority_timestamp, &entry::priority>,
				mi::member<entry, nano::block_hash, &entry::hash>
			>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<entry, nano::block_hash, &entry::hash>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<entry, nano::priority_timestamp, &entry::priority>>
	>>;
	// clang-format on

	ordered_entries pool;

	// Keep track of bucket sizes for efficient lookups
	boost::unordered_flat_map<nano::bucket_index, size_t, std::hash<nano::bucket_index>> bucket_sizes;
};
}