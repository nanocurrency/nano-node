#pragma once

#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>

namespace mi = boost::multi_index;

namespace nano
{
class election;
class active_elections;
class block;
}

namespace nano::scheduler
{
/** A class which holds an ordered set of blocks to be scheduled, ordered by their block arrival time
 */
class bucket final
{
public:
	using priority_t = uint64_t;

public:
	bucket (nano::uint128_t minimum_balance, nano::node &);
	~bucket ();

	nano::uint128_t const minimum_balance;

	bool available () const;
	bool activate ();
	void update ();

	bool push (uint64_t time, std::shared_ptr<nano::block> block);

	size_t size () const;
	size_t election_count () const;
	bool empty () const;
	void dump () const;

private:
	bool election_vacancy (priority_t candidate) const;
	bool election_overfill () const;
	void cancel_lowest_election ();

private: // Dependencies
	nano::active_elections & active;
	nano::stats & stats;

private: // Blocks
	struct block_entry
	{
		uint64_t time;
		std::shared_ptr<nano::block> block;

		bool operator< (block_entry const & other_a) const;
		bool operator== (block_entry const & other_a) const;
	};

	std::set<block_entry> queue;

private: // Elections
	struct election_entry
	{
		std::shared_ptr<nano::election> election;
		nano::qualified_root root;
		priority_t priority;
	};

	// clang-format off
	class tag_sequenced {};
	class tag_root {};
	class tag_priority {};

	using ordered_elections = boost::multi_index_container<election_entry,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<election_entry, nano::qualified_root, &election_entry::root>>,
		mi::ordered_non_unique<mi::tag<tag_priority>,
			mi::member<election_entry, priority_t, &election_entry::priority>>
	>>;
	// clang-format on

	ordered_elections elections;

private:
	mutable nano::mutex mutex;

private: // Config
	static size_t constexpr max_blocks{ 1024 * 8 };
	static size_t constexpr reserved_elections{ 100 };
	static size_t constexpr max_elections{ 150 };
};
} // namespace nano::scheduler
