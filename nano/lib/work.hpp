#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <memory>

namespace nano
{
std::string to_string (nano::work_version const version_a);

class block;
class block_details;
enum class block_type : uint8_t;

class opencl_work;
class work_item final
{
public:
	work_item (nano::work_version const version_a, nano::root const & item_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t> const &)> const & callback_a) :
		version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	nano::work_version const version;
	nano::root const item;
	uint64_t const difficulty;
	std::function<void (boost::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (nano::network_constants & network_constants, unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (nano::work_version const, nano::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (nano::root const &);
	void generate (nano::work_version const, nano::root const &, uint64_t, std::function<void (boost::optional<uint64_t> const &)>);
	boost::optional<uint64_t> generate (nano::work_version const, nano::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> generate (nano::root const &);
	boost::optional<uint64_t> generate (nano::root const &, uint64_t);
	size_t size ();
	nano::network_constants & network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<nano::work_item> pending;
	nano::mutex mutex{ mutex_identifier (mutexes::work_pool) };
	nano::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (nano::work_version const, nano::root const &, uint64_t, std::atomic<int> &)> opencl;
	nano::observer_set<bool> work_observers;
};

std::unique_ptr<container_info_component> collect_container_info (work_pool & work_pool, std::string const & name);
}
