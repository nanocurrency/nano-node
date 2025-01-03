#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>

namespace mi = boost::multi_index;

namespace nano::scheduler
{
class priority_bucket_config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	// Maximum number of blocks to sort by priority per bucket.
	std::size_t max_blocks{ 1024 * 8 };

	// Number of guaranteed slots per bucket available for election activation.
	std::size_t reserved_elections{ 100 };

	// Maximum number of slots per bucket available for election activation if the active election count is below the configured limit. (node.active_elections.size)
	std::size_t max_elections{ 150 };
};

/**
 * A class which holds an ordered set of blocks to be scheduled, ordered by their block arrival time
 * TODO: This combines both block ordering and election management, which makes the class harder to test. The functionality should be split.
 */
class bucket final
{
public:
	nano::bucket_index const index;

public:
	bucket (nano::bucket_index, priority_bucket_config const &, nano::active_elections &, nano::stats &);
	~bucket ();

	bool available () const;
	bool activate ();
	void update ();

	bool push (uint64_t time, std::shared_ptr<nano::block> block);

	bool contains (nano::block_hash const &) const;
	size_t size () const;
	size_t election_count () const;
	bool empty () const;
	std::deque<std::shared_ptr<nano::block>> blocks () const;

	void dump () const;

private:
	bool election_vacancy (nano::priority_timestamp candidate) const;
	bool election_overfill () const;
	void cancel_lowest_election ();

private: // Dependencies
	priority_bucket_config const & config;
	nano::active_elections & active;
	nano::stats & stats;

private: // Blocks
	struct block_entry
	{
		uint64_t time;
		std::shared_ptr<nano::block> block;

		nano::block_hash hash () const
		{
			return block->hash ();
		}

		// Keep operators inlined
		bool operator< (block_entry const & other) const
		{
			return time < other.time || (time == other.time && hash () < other.hash ());
		}
		bool operator== (block_entry const & other) const
		{
			return time == other.time && hash () == other.hash ();
		}
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};
	class tag_priority {};
	class tag_hash {};
	// clang-format on

	// clang-format off
	using ordered_blocks = boost::multi_index_container<block_entry,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::identity<block_entry>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::const_mem_fun<block_entry, nano::block_hash, &block_entry::hash>>
	>>;
	// clang-format on

	ordered_blocks queue;

private: // Elections
	struct election_entry
	{
		std::shared_ptr<nano::election> election;
		nano::qualified_root root;
		nano::priority_timestamp priority;
	};

	// clang-format off
	using ordered_elections = boost::multi_index_container<election_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<election_entry, nano::qualified_root, &election_entry::root>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<election_entry, nano::priority_timestamp, &election_entry::priority>>
	>>;
	// clang-format on

	ordered_elections elections;

private:
	mutable nano::mutex mutex;
};
}
