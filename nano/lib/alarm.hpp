#pragma once

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

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
	std::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	boost::thread thread;
};
class seq_con_info_component;
std::unique_ptr<seq_con_info_component> collect_seq_con_info (alarm & alarm, const std::string & name);
}
