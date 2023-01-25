#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/block_pipeline/account_state_filter.hpp>
#include <nano/node/block_pipeline/block_position_filter.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/epoch_restrictions_filter.hpp>
#include <nano/node/block_pipeline/link_filter.hpp>
#include <nano/node/block_pipeline/metastable_filter.hpp>
#include <nano/node/block_pipeline/receive_restrictions_filter.hpp>
#include <nano/node/block_pipeline/reserved_account_filter.hpp>
#include <nano/node/block_pipeline/send_restrictions_filter.hpp>
#include <nano/node/state_block_signature_verification.hpp>
#include <nano/secure/common.hpp>

#include <chrono>
#include <memory>
#include <thread>

namespace nano
{
class node;
class read_transaction;
class transaction;
class write_transaction;
class write_database_queue;

enum class block_origin
{
	local,
	remote
};

class block_post_events final
{
public:
	explicit block_post_events (std::function<nano::read_transaction ()> &&);
	~block_post_events ();
	std::deque<std::function<void (nano::read_transaction const &)>> events;

private:
	std::function<nano::read_transaction ()> get_transaction;
};

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	using value_type = block_pipeline::context;

	explicit block_processor (nano::node &, nano::write_database_queue &);
	void stop ();
	void flush ();
	std::size_t size ();
	bool full ();
	bool half_full ();
	void add (value_type & item);
	void add (std::shared_ptr<nano::block> const &);
	void force (std::shared_ptr<nano::block> const &);
	void wait_write ();
	bool should_log ();
	bool have_blocks_ready ();
	bool have_blocks ();
	void process_blocks ();
	nano::process_return process_one (nano::write_transaction const &, block_post_events &, value_type const & item, bool const = false, nano::block_origin const = nano::block_origin::remote);
	nano::process_return process_one (nano::write_transaction const &, block_post_events &, std::shared_ptr<nano::block> const &);
	std::atomic<bool> flushing{ false };
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };
	nano::observer_set<nano::transaction const &, nano::process_return const &, nano::block const &> processed;

private:
	void enqueue (value_type const & item);
	void queue_unchecked (nano::hash_or_account const &);
	void process_batch (nano::unique_lock<nano::mutex> &);
	void process_live (nano::transaction const &, nano::block_hash const &, std::shared_ptr<nano::block> const &, nano::process_return const &, nano::block_origin const = nano::block_origin::remote);
	void requeue_invalid (nano::block_hash const &, value_type const & item);
	void gap_previous_handler (nano::transaction const & transaction, std::shared_ptr<nano::block> block);
	bool stopped{ false };
	bool active{ false };
	bool awaiting_write{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<value_type> blocks;
	std::deque<std::shared_ptr<nano::block>> forced;
	nano::condition_variable condition;
	nano::node & node;
	nano::write_database_queue & write_database_queue;
	nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };

public: // Pipeline
	void pipeline_dump ();

private: // Pipeline
	std::function<void (value_type &)> pipeline;
	nano::block_pipeline::reserved_account_filter reserved;
	nano::block_pipeline::account_state_filter account_state;
	nano::block_pipeline::block_position_filter position;
	nano::block_pipeline::metastable_filter metastable;
	nano::block_pipeline::link_filter link;
	nano::block_pipeline::epoch_restrictions_filter epoch_restrictions;
	nano::block_pipeline::receive_restrictions_filter receive_restrictions;
	nano::block_pipeline::send_restrictions_filter send_restrictions;
	nano::state_block_signature_verification state_block_signature_verification;

private:
	std::thread processing_thread;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
};
std::unique_ptr<nano::container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
}
