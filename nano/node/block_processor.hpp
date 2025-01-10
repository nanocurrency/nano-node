#pragma once

#include <celerix/lib/logging.hpp>
#include <celerix/lib/thread_pool.hpp>
#include <celerix/node/fair_queue.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/secure/common.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace celerix
{
enum class block_source
{
	unknown = 0,
	live,
	live_originator,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,
	election,
};

std::string_view to_string (block_source);
celerix::stat::detail to_stat_detail (block_source);

class block_processor_config final
{
public:
	explicit block_processor_config (celerix::network_constants const &);

	celerix::error deserialize (celerix::tomlconfig & toml);
	celerix::error serialize (celerix::tomlconfig & toml) const;

public:
	// Maximum number of blocks to queue from network peers
	size_t max_peer_queue{ 128 };
	// Maximum number of blocks to queue from system components (local RPC, bootstrap)
	size_t max_system_queue{ 16 * 1024 };

	// Higher priority gets processed more frequently
	size_t priority_live{ 1 };
	size_t priority_bootstrap{ 8 };
	size_t priority_local{ 16 };
	size_t priority_system{ 32 };

	size_t batch_size{ 256 };
	size_t max_queued_notifications{ 8 };
};

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
		using result_t = celerix::block_status;
		using callback_t = std::function<void (result_t)>;

		context (std::shared_ptr<celerix::block> block, celerix::block_source source, callback_t callback = nullptr);

		std::shared_ptr<celerix::block> block;
		celerix::block_source source;
		callback_t callback;
		std::chrono::steady_clock::time_point arrival{ std::chrono::steady_clock::now () };

		std::future<result_t> get_future ();

	private:
		void set_result (result_t const &);
		std::promise<result_t> promise;

		friend class block_processor;
	};

public:
	block_processor (celerix::node_config const &, celerix::ledger &, celerix::unchecked_map &, celerix::stats &, celerix::logger &);
	~block_processor ();

	void start ();
	void stop ();

	std::size_t size () const;
	std::size_t size (celerix::block_source) const;
	bool add (std::shared_ptr<celerix::block> const &, celerix::block_source = celerix::block_source::live, std::shared_ptr<celerix::transport::channel> const & channel = nullptr, std::function<void (celerix::block_status)> callback = {});
	std::optional<celerix::block_status> add_blocking (std::shared_ptr<celerix::block> const & block, celerix::block_source);
	void force (std::shared_ptr<celerix::block> const &);

	celerix::container_info container_info () const;

	std::atomic<bool> flushing{ false };

public: // Events
	// All processed blocks including forks, rejected etc
	using processed_batch_t = std::deque<std::pair<celerix::block_status, context>>;
	using processed_batch_event_t = celerix::observer_set<processed_batch_t>;
	processed_batch_event_t batch_processed;

	// Rolled back blocks <rolled back blocks, root of rollback>
	using rolled_back_event_t = celerix::observer_set<std::deque<std::shared_ptr<celerix::block>>, celerix::qualified_root>;
	rolled_back_event_t rolled_back;

private: // Dependencies
	block_processor_config const & config;
	celerix::network_params const & network_params;
	celerix::ledger & ledger;
	celerix::unchecked_map & unchecked;
	celerix::stats & stats;
	celerix::logger & logger;

private:
	void run ();
	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (secure::write_transaction const &, celerix::block const & block);
	celerix::block_status process_one (secure::write_transaction const &, context const &, bool forced = false);
	processed_batch_t process_batch (celerix::unique_lock<celerix::mutex> &);
	std::deque<context> next_batch (size_t max_count);
	context next ();
	bool add_impl (context, std::shared_ptr<celerix::transport::channel> const & channel = nullptr);

private:
	celerix::fair_queue<context, celerix::block_source> queue;

	bool stopped{ false };
	celerix::condition_variable condition;
	mutable celerix::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	std::thread thread;

	celerix::thread_pool workers;
};
}
