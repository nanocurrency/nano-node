#pragma once

#include <nano/lib/threading.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>

namespace nano
{
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	nano::block_hash hash;
};

// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (nano::block_hash const &);
	bool recent (nano::block_hash const &);

	// clang-format off
	class tag_sequence {};
	class tag_hash {};

	boost::multi_index_container<nano::block_arrival_info,
		boost::multi_index::indexed_by<
			boost::multi_index::sequenced<boost::multi_index::tag<tag_sequence>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
				boost::multi_index::member<nano::block_arrival_info, nano::block_hash, &nano::block_arrival_info::hash>>>>
	arrival;
	// clang-format on

	nano::mutex mutex{ mutex_identifier (mutexes::block_arrival) };

	static std::size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<container_info_component> collect_container_info (block_arrival & block_arrival, std::string const & name);
}