#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
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

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
class cached_votes
{
public:
	std::chrono::steady_clock::time_point time;
	nano::block_hash hash;
	std::vector<std::shared_ptr<nano::vote>> votes;
};
class votes_cache
{
public:
	void add (std::shared_ptr<nano::vote> const &);
	std::vector<std::shared_ptr<nano::vote>> find (nano::block_hash const &);
	void remove (nano::block_hash const &);

private:
	std::mutex cache_mutex;
	boost::multi_index_container<
	nano::cached_votes,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<nano::cached_votes, std::chrono::steady_clock::time_point, &nano::cached_votes::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<nano::cached_votes, nano::block_hash, &nano::cached_votes::hash>>>>
	cache;
	static size_t constexpr max_cache = (nano::is_test_network) ? 2 : 1000;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
}
