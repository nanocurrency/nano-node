#pragma once

#include <nano/lib/interval.hpp>
#include <nano/node/block_context.hpp>
#include <nano/node/block_source.hpp>
#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/thread.hpp>

#include <any>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

namespace nano
{
class block_processor_config final
{
public:
	explicit block_processor_config (nano::network_constants const &);

	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	// Maximum number of blocks processed in a single batch (under a single write transaction)
	size_t batch_size{ 256 };

	// Maximum number of blocks to queue from network peers
	size_t max_peer_queue{ 128 };
	// Maximum number of blocks to queue from system components (local RPC, bootstrap)
	size_t max_system_queue{ 128 * 1024 };

	// Higher priority gets processed more frequently
	size_t priority_live{ 1 };
	size_t priority_bootstrap{ 8 };
	size_t priority_local{ 16 };
	size_t priority_system{ 32 };

	bool enable_throttling{ true };
	double backlog_threshold{ 1.5 };
	std::chrono::milliseconds backlog_throttle{ 30ms };
	std::chrono::milliseconds backlog_throttle_max{ 1s };
};

/**
 * Processes blocks into the ledger on a dedicated thread.
 * Blocks are queued in a fair queue that balances between sources (live, bootstrap, local, forced, ...) according to configured priorities and per-source size limits.
 * Blocks that cannot be processed yet (gaps) are stored in the unchecked map and requeued once their dependencies are satisfied.
 * Processing results are dispatched asynchronously via ledger_notifications.
 */
class block_processor final
{
public:
	block_processor (nano::node_config const &, nano::ledger &, nano::ledger_notifications &, nano::unchecked_map &, nano::stats &, nano::logger &);
	~block_processor ();

	void start ();
	void stop ();

	/// Queues a block for processing, returns false if the block was dropped (insufficient work or queue overfill)
	bool add (
	std::shared_ptr<nano::block> const & block,
	nano::block_source source,
	std::shared_ptr<nano::transport::channel> const & channel = nullptr,
	std::function<void (nano::block_status)> callback = nullptr,
	std::any tag = {});

	/// Result of a batch add operation
	struct add_many_result
	{
		std::size_t added;
		std::size_t dropped; /// Number of blocks that did not make it into the queue (queue overfill or invalid work)
	};

	/// Queues multiple blocks under a single lock, attaching the callback to the last valid block
	add_many_result add_many (
	std::deque<std::shared_ptr<nano::block>> const & blocks,
	nano::block_source source,
	std::shared_ptr<nano::transport::channel> const & channel = nullptr,
	std::function<void (nano::block_status)> last_callback = nullptr,
	std::any tag = {});

	/// Queues a block and waits for its processing result, returns nullopt if the block was dropped
	std::optional<nano::block_status> add_blocking (
	std::shared_ptr<nano::block> const & block,
	nano::block_source source,
	std::any tag = {});

	/// Queues a block for processing that rolls back any competing fork block already in the ledger
	void force (std::shared_ptr<nano::block> const & block);

	/// Total number of queued blocks
	std::size_t size () const;
	/// Number of queued blocks from the given source
	std::size_t size (nano::block_source) const;
	/// Number of queued blocks in the fair queue bucket for the given source and channel
	std::size_t size (nano::block_source, std::shared_ptr<nano::transport::channel> const & channel) const;

	nano::container_info container_info () const;

private: // Dependencies
	block_processor_config const & config;
	nano::node_config const & node_config;
	nano::network_params const & network_params;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::unchecked_map & unchecked;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();

	/// Processes a single block into the ledger and handles the result (e.g. stores gapped blocks as unchecked)
	nano::block_status process_one (secure::write_transaction const &, nano::block_context const &, bool forced = false);
	/// Processes the next batch of blocks under a single write transaction and queues result notifications
	void process_batch (nano::unique_lock<nano::mutex> &);
	/// Pops up to max_count blocks from the queue
	std::deque<nano::block_context> next_batch (size_t max_count);
	/// Pops the next block from the queue
	nano::block_context next ();

	/// Rolls back the ledger block occupying the same qualified root, together with its dependents
	void rollback_competitor (secure::write_transaction &, nano::block const & block);

	/// Pushes a block context onto the queue and notifies the processing thread, returns false on overfill
	bool add_impl (nano::block_context, std::shared_ptr<nano::transport::channel> const & channel = nullptr);

	/// Throttles processing when the ledger backlog exceeds the configured threshold
	void wait_backlog (nano::unique_lock<nano::mutex> &);

	/// Ratio of ledger backlog to the throttling threshold, 0 when below the threshold
	double backlog_factor () const;

private:
	/// Queue of blocks to be processed, fairly balanced between sources
	nano::fair_queue<nano::block_context, nano::block_source, std::shared_ptr<nano::transport::channel>> queue;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	boost::thread thread;

	nano::interval log_processing_interval;
	nano::interval log_backlog_interval;
	nano::interval log_cooldown_interval;
};
}
