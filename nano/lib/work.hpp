#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace nano
{
class block;
bool work_validate (nano::block_hash const &, uint64_t, uint64_t * = nullptr);
bool work_validate (nano::block const &, uint64_t * = nullptr);
uint64_t work_value (nano::block_hash const &, uint64_t);
class opencl_work;
class work_item final
{
public:
	nano::uint256_union item;
	std::function<void(boost::optional<uint64_t> const &)> callback;
	uint64_t difficulty;
};
class work_pool final
{
public:
	work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (nano::uint256_union const &, uint64_t)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (nano::uint256_union const &);
	void generate (nano::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>);
	void generate (nano::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>, uint64_t);
	uint64_t generate (nano::uint256_union const &);
	uint64_t generate (nano::uint256_union const &, uint64_t);
	nano::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<nano::work_item> pending;
	std::mutex mutex;
	std::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (nano::uint256_union const &, uint64_t)> opencl;
	nano::observer_set<bool> work_observers;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (work_pool & work_pool, const std::string & name);
}
