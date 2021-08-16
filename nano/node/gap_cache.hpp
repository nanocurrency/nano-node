#pragma once

//#include <bits/shared_ptr.h>                    // for shared_ptr
#include <boost/mpl/eval_if.hpp>

#include <cstddef> // for size_t
//#include <boost/multi_index/hashed_index_fwd.hpp>
//#include <boost/multi_index/detail/hash_index_args.hpp>
//#include <boost/multi_index/hashed_index.hpp>   // for hashed_unique
//#include <boost/multi_index/indexed_by.hpp>     // for indexed_by
//#include <boost/multi_index/ordered_index.hpp>  // for ordered_non_unique
//#include <boost/multi_index/tag.hpp>            // for tag
//#include <boost/multi_index_container.hpp>      // for multi_index_container

#include "nano/lib/locks.hpp" // for mutex_identifier, mutex

#include <nano/lib/numbers.hpp> // for account, block_hash

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono> // for steady_clock, steady_...
#include <memory> // for unique_ptr
#include <string> // for string
#include <vector> // for vector

namespace boost
{
namespace multi_index
{
	template <class Class, typename Type, Type Class::*PtrToMember>
	struct member;
}
}
namespace nano
{
class container_info_component;
}
namespace nano
{
class vote;
}

namespace nano
{
class node;
class transaction;

/** For each gap in account chains, track arrival time and voters */
class gap_information final
{
public:
	std::chrono::steady_clock::time_point arrival;
	nano::block_hash hash;
	std::vector<nano::account> voters;
	bool bootstrap_started{ false };
};

/** Maintains voting and arrival information for gaps (missing source or previous blocks in account chains) */
class gap_cache final
{
public:
	explicit gap_cache (nano::node &);
	void add (nano::block_hash const &, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());
	void erase (nano::block_hash const & hash_a);
	void vote (std::shared_ptr<nano::vote> const &);
	bool bootstrap_check (std::vector<nano::account> const &, nano::block_hash const &);
	void bootstrap_start (nano::block_hash const & hash_a);
	nano::uint128_t bootstrap_threshold ();
	size_t size ();
	// clang-format off
	class tag_arrival {};
	class tag_hash {};
	using ordered_gaps = boost::multi_index_container<nano::gap_information,
	boost::multi_index::indexed_by<
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<tag_arrival>,
			boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
			boost::multi_index::member<gap_information, nano::block_hash, &gap_information::hash>>>>;
	ordered_gaps blocks;
	// clang-format on
	size_t const max = 256;
	nano::mutex mutex{ mutex_identifier (mutexes::gap_cache) };
	nano::node & node;
};

std::unique_ptr<container_info_component> collect_container_info (gap_cache & gap_cache, std::string const & name);
}
