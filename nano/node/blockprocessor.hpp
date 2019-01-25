#pragma once

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <chrono>
#include <memory>
#include <nano/lib/blocks.hpp>
#include <nano/node/voting.hpp>
#include <nano/secure/common.hpp>
#include <unordered_set>

namespace nano
{
class node;
class transaction;

class rolled_hash
{
public:
	std::chrono::steady_clock::time_point time;
	nano::block_hash hash;
};

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor
{
public:
	block_processor (nano::node &);
	~block_processor ();
	void stop ();
	void flush ();
	bool full ();
	void add (std::shared_ptr<nano::block>, std::chrono::steady_clock::time_point);
	void force (std::shared_ptr<nano::block>);
	bool should_log (bool);
	bool have_blocks ();
	void process_blocks ();
	nano::process_return process_one (nano::transaction const &, std::shared_ptr<nano::block>, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now (), bool = false);

private:
	void queue_unchecked (nano::transaction const &, nano::block_hash const &, std::chrono::steady_clock::time_point = std::chrono::steady_clock::time_point ());
	void verify_state_blocks (nano::transaction const & transaction_a, std::unique_lock<std::mutex> &, size_t = std::numeric_limits<size_t>::max ());
	void process_batch (std::unique_lock<std::mutex> &);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<std::pair<std::shared_ptr<nano::block>, std::chrono::steady_clock::time_point>> state_blocks;
	std::deque<std::pair<std::shared_ptr<nano::block>, std::chrono::steady_clock::time_point>> blocks;
	std::unordered_set<nano::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<nano::block>> forced;
	boost::multi_index_container<
	nano::rolled_hash,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<nano::rolled_hash, std::chrono::steady_clock::time_point, &nano::rolled_hash::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<nano::rolled_hash, nano::block_hash, &nano::rolled_hash::hash>>>>
	rolled_back;
	static size_t const rolled_back_max = 1024;
	std::condition_variable condition;
	nano::node & node;
	nano::vote_generator generator;
	std::mutex mutex;
};
}
