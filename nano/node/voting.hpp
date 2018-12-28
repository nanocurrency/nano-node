#pragma once

#include <nano/lib/numbers.hpp>

#include <boost/thread.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace nano
{
class node;
class vote_generator
{
public:
	vote_generator (nano::node &, std::chrono::milliseconds);
	void add (nano::block_hash const &);
	void stop ();

private:
	void run ();
	void send (std::unique_lock<std::mutex> &);
	nano::node & node;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<nano::block_hash> hashes;
	std::chrono::milliseconds wait;
	bool stopped;
	bool started;
	boost::thread thread;
};
}
