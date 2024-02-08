#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/secure/common.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace nano::store
{
class write_transaction;
}

namespace nano
{
class node;
class write_database_queue;

enum class block_source
{
	unknown = 0,
	live,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,
};

nano::stat::detail to_stat_detail (block_source);

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public: // Context
	class context
	{
	public:
		explicit context (block_source);

		block_source const source;
		std::chrono::steady_clock::time_point const arrival{ std::chrono::steady_clock::now () };

	public:
		using result_t = nano::process_return;
		std::future<result_t> get_future ();

	private:
		void set_result (result_t const &);
		std::promise<result_t> promise;

		friend class block_processor;
	};

private:
	struct entry
	{
		std::shared_ptr<nano::block> block;
		block_processor::context ctx;
	};

public:
	block_processor (nano::node &, nano::write_database_queue &);

	void stop ();
	std::size_t size ();
	bool full ();
	bool half_full ();
	void add (std::shared_ptr<nano::block> const &, block_source = block_source::live);
	std::optional<nano::process_return> add_blocking (std::shared_ptr<nano::block> const & block, block_source);
	void force (std::shared_ptr<nano::block> const &);
	bool should_log ();
	bool have_blocks_ready ();
	bool have_blocks ();
	void process_blocks ();

	std::atomic<bool> flushing{ false };

public: // Events
	using processed_t = std::tuple<nano::process_return, std::shared_ptr<nano::block>, context>;
	using processed_batch_t = std::deque<processed_t>;

	// The batch observer feeds the processed obsever
	nano::observer_set<nano::process_return const &, std::shared_ptr<nano::block> const &, context const &> processed;
	nano::observer_set<processed_batch_t const &> batch_processed;

private:
	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (store::write_transaction const &, nano::block const & block);
	nano::process_return process_one (store::write_transaction const &, std::shared_ptr<nano::block> block, context const &, bool forced = false);
	void queue_unchecked (store::write_transaction const &, nano::hash_or_account const &);
	processed_batch_t process_batch (nano::unique_lock<nano::mutex> &);
	entry next ();
	void add_impl (std::shared_ptr<nano::block> block, context);

private: // Dependencies
	nano::node & node;
	nano::write_database_queue & write_database_queue;

private:
	bool stopped{ false };
	bool active{ false };

	std::deque<entry> blocks;
	std::deque<entry> forced;

	std::chrono::steady_clock::time_point next_log;
	nano::condition_variable condition;
	nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	std::thread processing_thread;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
};
}
