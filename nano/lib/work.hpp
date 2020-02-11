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
enum class work_version
{
	unspecified,
	work_1
};
std::string to_string (nano::work_version const version_a);

class block;
bool work_validate (nano::work_version const, nano::block const &, uint64_t * = nullptr);
bool work_validate (nano::work_version const, nano::root const &, uint64_t const, uint64_t * = nullptr);
bool work_validate_entry (nano::block const &);
bool work_validate_entry (nano::root const &, uint64_t const);
// For tests only
bool work_validate (nano::block const &, uint64_t * = nullptr);
// For tests only
bool work_validate (nano::root const &, uint64_t const, uint64_t * = nullptr);

namespace work_v1
{
	bool validate (nano::root const &, uint64_t const, uint64_t * = nullptr);
	uint64_t value (nano::root const &, uint64_t);
}
class opencl_work;
class work_item final
{
public:
	work_item (nano::work_version const version_a, nano::root const & item_a, std::function<void(boost::optional<uint64_t> const &)> const & callback_a, uint64_t difficulty_a) :
	version (version_a), item (item_a), callback (callback_a), difficulty (difficulty_a)
	{
	}
	nano::work_version version;
	nano::root item;
	std::function<void(boost::optional<uint64_t> const &)> callback;
	uint64_t difficulty;
};
class work_pool final
{
public:
	explicit work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (nano::work_version const, nano::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (nano::root const &);
	void generate (nano::work_version const, nano::root const &, std::function<void(boost::optional<uint64_t> const &)>);
	void generate (nano::work_version const, nano::root const &, std::function<void(boost::optional<uint64_t> const &)>, uint64_t);
	boost::optional<uint64_t> generate (nano::work_version const, nano::root const &);
	boost::optional<uint64_t> generate (nano::work_version const, nano::root const &, uint64_t);
	// Duplicates with a default work_version for tests
	void generate (nano::root const &, std::function<void(boost::optional<uint64_t> const &)>);
	void generate (nano::root const &, std::function<void(boost::optional<uint64_t> const &)>, uint64_t);
	boost::optional<uint64_t> generate (nano::root const &);
	boost::optional<uint64_t> generate (nano::root const &, uint64_t);
	size_t size ();
	nano::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<work_item> pending;
	std::mutex mutex;
	nano::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (nano::work_version const, nano::root const &, uint64_t, std::atomic<int> &)> opencl;
	nano::observer_set<bool> work_observers;
};

std::unique_ptr<container_info_component> collect_container_info (work_pool & work_pool, const std::string & name);
}
