#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/numbers_templ.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>

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

namespace celerix::scheduler
{
class priority_bucket_config final
{
public:
	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

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
	celerix::bucket_index const index;

public:
	bucket (celerix::bucket_index, priority_bucket_config const &, celerix::active_elections &, celerix::stats &);
	~bucket ();

	bool available () const;
	bool activate ();
	void update ();

	bool push (uint64_t time, std::shared_ptr<celerix::block> block);

	bool contains (celerix::block_hash const &) const;
	size_t size () const;
	size_t election_count () const;
	bool empty () const;
	std::deque<std::shared_ptr<celerix::block>> blocks () const;

	void dump () const;

private:
	bool election_vacancy (celerix::priority_timestamp candidate) const;
	bool election_overfill () const;
	void cancel_lowest_election ();

private: // Dependencies
	priority_bucket_config const & config;
	celerix::active_elections & active;
	celerix::stats & stats;

private: // Blocks
	struct block_entry
	{
		uint64_t time;
		std::shared_ptr<celerix::block> block;

		celerix::block_hash hash () const
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
			mi::const_mem_fun<block_entry, celerix::block_hash, &block_entry::hash>>
	>>;
	// clang-format on

	ordered_blocks queue;

private: // Elections
	struct election_entry
	{
		std::shared_ptr<celerix::election> election;
		celerix::qualified_root root;
		celerix::priority_timestamp priority;
	};

	// clang-format off
	using ordered_elections = boost::multi_index_container<election_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<election_entry, celerix::qualified_root, &election_entry::root>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<election_entry, celerix::priority_timestamp, &election_entry::priority>>
	>>;
	// clang-format on

	ordered_elections elections;

private:
	mutable celerix::mutex mutex;
};
}
