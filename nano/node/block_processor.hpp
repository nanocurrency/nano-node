#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/block_context.hpp>
#include <nano/node/block_source.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <boost/thread.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace nano
{
class block_processor_config final
{
public:
	explicit block_processor_config (nano::network_constants const &);

	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	size_t batch_size{ 256 };

	// Maximum number of blocks to queue from network peers
	size_t max_peer_queue{ 128 };
	// Maximum number of blocks to queue from system components (local RPC, bootstrap)
	size_t max_system_queue{ 16 * 1024 };

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
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	block_processor (nano::node_config const &, nano::ledger &, nano::ledger_notifications &, nano::unchecked_map &, nano::stats &, nano::logger &);
	~block_processor ();

	void start ();
	void stop ();

	std::size_t size () const;
	std::size_t size (nano::block_source) const;
	bool add (std::shared_ptr<nano::block> const &, nano::block_source = nano::block_source::live, std::shared_ptr<nano::transport::channel> const & channel = nullptr, std::function<void (nano::block_status)> callback = {});
	std::optional<nano::block_status> add_blocking (std::shared_ptr<nano::block> const & block, nano::block_source);
	void force (std::shared_ptr<nano::block> const &);

	nano::container_info container_info () const;

	std::atomic<bool> flushing{ false };

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

	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (secure::write_transaction &, nano::block const & block);
	nano::block_status process_one (secure::write_transaction const &, nano::block_context const &, bool forced = false);
	void process_batch (nano::unique_lock<nano::mutex> &);
	std::deque<nano::block_context> next_batch (size_t max_count);
	nano::block_context next ();
	bool add_impl (nano::block_context, std::shared_ptr<nano::transport::channel> const & channel = nullptr);

	void wait_backlog (nano::unique_lock<nano::mutex> &);
	double backlog_factor () const;

private:
	nano::fair_queue<nano::block_context, nano::block_source> queue;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	boost::thread thread;

	nano::interval log_processing_interval;
	nano::interval log_backlog_interval;
};
}
