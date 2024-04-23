#pragma once

#include <nano/lib/timer.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace nano
{
class block;
class logger;
}
namespace nano::scheduler
{
/** A class which holds an ordered set of blocks to be scheduled, ordered by their block arrival time
 */
class bucket final
{
	class tag_time
	{
	};
	class tag_hash
	{
	};
	class entry_t
	{
	public:
		std::chrono::steady_clock::time_point time;
		std::shared_ptr<nano::block> block;
		nano::block_hash hash () const;
	};
	using backlog_t = boost::multi_index_container<entry_t,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::tag<tag_time>,
	boost::multi_index::member<entry_t, std::chrono::steady_clock::time_point, &entry_t::time>,
	std::greater<std::chrono::steady_clock::time_point>>, // Sorted by last-confirmed time in descending order
	boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
	boost::multi_index::const_mem_fun<entry_t, nano::block_hash, &entry_t::hash>>>>;

public:
	bucket (size_t max);
	std::shared_ptr<nano::block> insert (std::chrono::steady_clock::time_point time, std::shared_ptr<nano::block> block);
	size_t erase (nano::block_hash const & hash);

private:
	backlog_t backlog;
	std::mutex mutex;
	size_t const max;
};
} // namespace nano::scheduler
