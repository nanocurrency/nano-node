#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/utility.hpp>

#include <chrono>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace nano
{
/** An alarm operation is a function- and invocation time pair. Operations are ordered chronologically. */
class operation final
{
public:
	bool operator> (nano::operation const &) const;
	std::chrono::steady_clock::time_point wakeup;
	std::function<void()> function;
};

/** General purpose timer to defer operations */
class alarm final
{
public:
	explicit alarm (boost::asio::io_context &);
	~alarm ();
	void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);
	void run ();
	boost::asio::io_context & io_ctx;
	std::mutex mutex;
	nano::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	std::thread thread;
};
class container_info_component;
std::unique_ptr<container_info_component> collect_container_info (alarm & alarm, const std::string & name);
}
