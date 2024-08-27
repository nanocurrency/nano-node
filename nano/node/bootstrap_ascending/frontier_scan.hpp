#pragma once

#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap_ascending/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <map>
#include <set>

namespace mi = boost::multi_index;

namespace nano::bootstrap_ascending
{
class frontier_scan
{
public:
	frontier_scan (frontier_scan_config const &, nano::stats &);

	nano::account next ();
	bool process (nano::account start, std::deque<std::pair<nano::account, nano::block_hash>> const & response);

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	frontier_scan_config const & config;
	nano::stats & stats;

private:
	struct frontier_head
	{
		frontier_head (nano::account start_a, nano::account end_a) :
			start{ start_a },
			end{ end_a },
			next{ start_a }
		{
		}

		// The range of accounts to scan is [start, end)
		nano::account start;
		nano::account end;

		nano::account next;
		std::set<nano::account> candidates;

		unsigned requests{ 0 };
		unsigned completed{ 0 };
		std::chrono::steady_clock::time_point timestamp{};
		size_t processed{ 0 }; // Total number of accounts processed
	};

	// clang-format off
	class tag_sequenced {};
	class tag_start {};
	class tag_timestamp {};

	using ordered_heads = boost::multi_index_container<frontier_head,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_start>,
			mi::member<frontier_head, nano::account, &frontier_head::start>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<frontier_head, std::chrono::steady_clock::time_point, &frontier_head::timestamp>>
	>>;
	// clang-format on

	ordered_heads heads;
};
}