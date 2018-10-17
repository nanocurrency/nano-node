#pragma once

#include <rai/lib/numbers.hpp>

#include <boost/thread.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace rai
{
class node;
class vote_generator
{
public:
	vote_generator (rai::node &, std::chrono::milliseconds);
	void add (rai::block_hash const &);
	void stop ();

private:
	void run ();
	void send (std::unique_lock<std::mutex> &);
	rai::node & node;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<rai::block_hash> hashes;
	std::chrono::milliseconds wait;
	bool stopped;
	bool started;
	boost::thread thread;
};
}
