#pragma once

#include <nano/lib/logging.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace nano
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
};

std::string_view to_string (block_source);
nano::stat::detail to_stat_detail (block_source);

class block_processor_config final
{
public:
	explicit block_processor_config (nano::network_constants const &);

	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

public:
	// Maximum number of blocks to queue from network peers
	size_t max_peer_queue{ 128 };
	// Maximum number of blocks to queue from system components (local RPC, bootstrap)
	size_t max_system_queue{ 16 * 1024 };

	// Higher priority gets processed more frequently
	size_t priority_live{ 1 };
	size_t priority_bootstrap{ 8 };
	size_t priority_local{ 16 };
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
		using result_t = nano::block_status;
		using callback_t = std::function<void (result_t)>;

		context (std::shared_ptr<nano::block> block, nano::block_source source, callback_t callback = nullptr);

		std::shared_ptr<nano::block> block;
		nano::block_source source;
		callback_t callback;
		std::chrono::steady_clock::time_point arrival{ std::chrono::steady_clock::now () };

		std::future<result_t> get_future ();

	private:
		void set_result (result_t const &);
		std::promise<result_t> promise;

		friend class block_processor;
	};

public:
	block_processor (nano::node_config const &, nano::ledger &, nano::unchecked_map &, nano::stats &, nano::logger &);
	~block_processor ();

	void start ();
	void stop ();

	std::size_t size () const;
	std::size_t size (nano::block_source) const;
	bool add (std::shared_ptr<nano::block> const &, nano::block_source = nano::block_source::live, std::shared_ptr<nano::transport::channel> const & channel = nullptr, std::function<void (nano::block_status)> callback = {});
	std::optional<nano::block_status> add_blocking (std::shared_ptr<nano::block> const & block, nano::block_source);
	void force (std::shared_ptr<nano::block> const &);
	bool should_log ();

	nano::container_info container_info () const;

	std::atomic<bool> flushing{ false };

public: // Events
	using processed_t = std::tuple<nano::block_status, context>;
	using processed_batch_t = std::deque<processed_t>;

	// The batch observer feeds the processed observer
	nano::observer_set<nano::block_status const &, context const &> block_processed;
	nano::observer_set<processed_batch_t const &> batch_processed;

	// Rolled back blocks <rolled back block, root of rollback>
	nano::observer_set<std::shared_ptr<nano::block> const &, nano::qualified_root const &> rolled_back;

private: // Dependencies
	block_processor_config const & config;
	nano::network_params const & network_params;
	nano::ledger & ledger;
	nano::unchecked_map & unchecked;
	nano::stats & stats;
	nano::logger & logger;

private:
	void run ();
	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (secure::write_transaction const &, nano::block const & block);
	nano::block_status process_one (secure::write_transaction const &, context const &, bool forced = false);
	processed_batch_t process_batch (nano::unique_lock<nano::mutex> &);
	std::deque<context> next_batch (size_t max_count);
	context next ();
	bool add_impl (context, std::shared_ptr<nano::transport::channel> const & channel = nullptr);

private:
	nano::fair_queue<context, nano::block_source> queue;

	std::chrono::steady_clock::time_point next_log{ std::chrono::steady_clock::now () };

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	std::thread thread;
};
}
