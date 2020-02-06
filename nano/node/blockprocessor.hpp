#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/voting.hpp>
#include <nano/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <unordered_set>

namespace nano
{
class node;
class transaction;
class write_transaction;
class write_database_queue;

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	explicit block_processor (nano::node &, nano::write_database_queue &);
	~block_processor ();
	void stop ();
	void flush ();
	size_t size ();
	bool full ();
	bool half_full ();
	void add (nano::unchecked_info const &);
	void add (std::shared_ptr<nano::block>, uint64_t = 0);
	void force (std::shared_ptr<nano::block>);
	void wait_write ();
	bool should_log (bool);
	bool have_blocks ();
	void process_blocks ();
	nano::process_return process_one (nano::write_transaction const &, nano::unchecked_info, const bool = false);
	nano::process_return process_one (nano::write_transaction const &, std::shared_ptr<nano::block>, const bool = false);
	nano::vote_generator generator;
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

private:
	void queue_unchecked (nano::write_transaction const &, nano::block_hash const &);
	void verify_state_blocks (nano::unique_lock<std::mutex> &, size_t = std::numeric_limits<size_t>::max ());
	void process_batch (nano::unique_lock<std::mutex> &);
	void process_live (nano::block_hash const &, std::shared_ptr<nano::block>, const bool = false);
	void requeue_invalid (nano::block_hash const &, nano::unchecked_info const &);
	bool stopped;
	bool active;
	bool awaiting_write{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<nano::unchecked_info> state_blocks;
	std::deque<nano::unchecked_info> blocks;
	std::deque<std::shared_ptr<nano::block>> forced;
	nano::condition_variable condition;
	nano::node & node;
	nano::write_database_queue & write_database_queue;
	std::mutex mutex;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, const std::string & name);
};
}
