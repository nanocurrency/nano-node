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
/*
 * Frontier scan divides the account space into ranges and scans each range for outdated frontiers in parallel.
 * This class is used to track the progress of each range.
 */
class frontier_scan
{
public:
	frontier_scan (frontier_scan_config const &, nano::stats &);

	nano::account next ();
	bool process (nano::account start, std::deque<std::pair<nano::account, nano::block_hash>> const & response);

	nano::container_info container_info () const;

private: // Dependencies
	frontier_scan_config const & config;
	nano::stats & stats;

private:
	// Represents a range of accounts to scan, once the full range is scanned (goes past `end`) the head wraps around (to the `start`)
	struct frontier_head
	{
		frontier_head (nano::account start_a, nano::account end_a) :
			start{ start_a },
			end{ end_a },
			next{ start_a }
		{
		}

		// The range of accounts to scan is [start, end)
		nano::account const start;
		nano::account const end;

		// We scan the range by querying frontiers starting at 'next' and gathering candidates
		nano::account next;
		std::set<nano::account> candidates;

		unsigned requests{ 0 };
		unsigned completed{ 0 };
		std::chrono::steady_clock::time_point timestamp{};
		size_t processed{ 0 }; // Total number of accounts processed

		nano::account index () const
		{
			return start;
		}
	};

	// clang-format off
	class tag_sequenced {};
	class tag_start {};
	class tag_timestamp {};

	using ordered_heads = boost::multi_index_container<frontier_head,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_sequenced>>,
		mi::ordered_unique<mi::tag<tag_start>,
			mi::const_mem_fun<frontier_head, nano::account, &frontier_head::index>>,
		mi::ordered_non_unique<mi::tag<tag_timestamp>,
			mi::member<frontier_head, std::chrono::steady_clock::time_point, &frontier_head::timestamp>>
	>>;
	// clang-format on

	ordered_heads heads;
};
}