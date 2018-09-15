#pragma once

#include <boost/optional.hpp>
#include <galileo/lib/config.hpp>
#include <galileo/lib/numbers.hpp>
#include <galileo/lib/utility.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

namespace galileo
{
class block;
bool work_validate (galileo::block_hash const &, uint64_t);
bool work_validate (galileo::block const &);
uint64_t work_value (galileo::block_hash const &, uint64_t);
class opencl_work;
class work_pool
{
public:
	work_pool (unsigned, std::function<boost::optional<uint64_t> (galileo::uint256_union const &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (galileo::uint256_union const &);
	void generate (galileo::uint256_union const &, std::function<void(boost::optional<uint64_t> const &)>);
	uint64_t generate (galileo::uint256_union const &);
	std::atomic<int> ticket;
	bool done;
	std::vector<std::thread> threads;
	std::list<std::pair<galileo::uint256_union, std::function<void(boost::optional<uint64_t> const &)>>> pending;
	std::mutex mutex;
	std::condition_variable producer_condition;
	std::function<boost::optional<uint64_t> (galileo::uint256_union const &)> opencl;
	galileo::observer_set<bool> work_observers;
	// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
	static uint64_t const publish_test_threshold = 0xff00000000000000;
	static uint64_t const publish_full_threshold = 0xffffffc000000000;
	static uint64_t const publish_threshold = galileo::galileo_network == galileo::galileo_networks::galileo_test_network ? publish_test_threshold : publish_full_threshold;
};
}
