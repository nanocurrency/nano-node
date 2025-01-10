#pragma once

#include <celerix/lib/config.hpp>
#include <celerix/lib/locks.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/observer_set.hpp>
#include <celerix/lib/utility.hpp>
#include <celerix/node/openclwork.hpp>

#include <boost/optional.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace celerix
{
std::string to_string (celerix::work_version const version_a);

// type of function that does the work generation with an optional return value
using opencl_work_func_t = std::function<boost::optional<uint64_t> (celerix::work_version const, celerix::root const &, uint64_t, std::atomic<int> &)>;

class block;
class block_details;
enum class block_type : uint8_t;

class opencl_work;
class work_item final
{
public:
	work_item (celerix::work_version const version_a, celerix::root const & item_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t> const &)> const & callback_a) :
		version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	celerix::work_version const version;
	celerix::root const item;
	uint64_t const difficulty;
	std::function<void (boost::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (celerix::network_constants & network_constants, unsigned, std::chrono::celerixseconds = std::chrono::celerixseconds (0), celerix::opencl_work_func_t = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (celerix::root const &);
	void generate (celerix::work_version const, celerix::root const &, uint64_t, std::function<void (boost::optional<uint64_t> const &)>);
	boost::optional<uint64_t> generate (celerix::work_version const, celerix::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> generate (celerix::root const &);
	boost::optional<uint64_t> generate (celerix::root const &, uint64_t);
	size_t size ();
	celerix::network_constants & network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<std::thread> threads;
	std::list<celerix::work_item> pending;
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::work_pool) };
	celerix::condition_variable producer_condition;
	std::chrono::celerixseconds pow_rate_limiter;
	celerix::opencl_work_func_t opencl;
	celerix::observer_set<bool> work_observers;

	celerix::container_info container_info () const;
};
}
