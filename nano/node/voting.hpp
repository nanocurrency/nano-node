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
	void cache_add (std::shared_ptr<nano::vote> const &);
	std::vector<std::shared_ptr<nano::vote>> cache_find (nano::block_hash const &)

private:
	void run ();
	void send (std::unique_lock<std::mutex> &);
	nano::node & node;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<nano::block_hash> hashes;
	std::unordered_map<nano::block_hash, std::vector<std::shared_ptr<nano::vote>>> votes_cache;
	std::deque<nano::block_hash> cache_order;
	size_t max_cache = (nano::nano_network == nano::nano_networks::nano_test_network) ? 2 : 1000;
	std::chrono::milliseconds wait;
	bool stopped;
	bool started;
	boost::thread thread;
};
}
