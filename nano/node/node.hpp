#pragma once

#include <nano/lib/block_uniquer.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/backlog_population.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap/bootstrap.hpp>
#include <nano/node/bootstrap/bootstrap_attempt.hpp>
#include <nano/node/bootstrap/bootstrap_server.hpp>
#include <nano/node/distributed_work_factory.hpp>
#include <nano/node/epoch_upgrader.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/process_live_dispatcher.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/telemetry.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/wallet.hpp>
#include <nano/node/websocket.hpp>
#include <nano/secure/utility.hpp>

#include <boost/program_options.hpp>
#include <boost/thread/latch.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace nano
{
class active_elections;
class confirming_set;
class message_processor;
class monitor;
class node;
class online_reps;
class vote_processor;
class vote_cache_processor;
class vote_router;
class work_pool;
class peer_history;
class port_mapping;
class thread_runner;

namespace scheduler
{
	class component;
}
namespace transport
{
	class tcp_listener;
}
namespace bootstrap_ascending
{
	class service;
}
namespace rocksdb
{
} // Declare a namespace rocksdb inside nano so all references to the rocksdb library need to be globally scoped e.g. ::rocksdb::Slice
}

namespace nano
{
// Configs
backlog_population::config backlog_population_config (node_config const &);
outbound_bandwidth_limiter::config outbound_bandwidth_limiter_config (node_config const &);

class node final : public std::enable_shared_from_this<node>
{
public:
	node (std::shared_ptr<boost::asio::io_context>, uint16_t peering_port, std::filesystem::path const & application_path, nano::work_pool &, nano::node_flags = nano::node_flags (), unsigned seq = 0);
	node (std::shared_ptr<boost::asio::io_context>, std::filesystem::path const & application_path, nano::node_config const &, nano::work_pool &, nano::node_flags = nano::node_flags (), unsigned seq = 0);
	~node ();

public:
	template <typename T>
	void background (T action_a)
	{
		io_ctx.post (action_a);
	}
	bool copy_with_compaction (std::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<nano::node> shared ();
	int store_version ();
	void process_confirmed (nano::election_status const &, uint64_t = 0);
	void process_active (std::shared_ptr<nano::block> const &);
	std::optional<nano::block_status> process_local (std::shared_ptr<nano::block> const &);
	void process_local_async (std::shared_ptr<nano::block> const &);
	void keepalive_preconfigured ();
	std::shared_ptr<nano::block> block (nano::block_hash const &);
	bool block_or_pruned_exists (nano::block_hash const &) const;
	std::pair<nano::uint128_t, nano::uint128_t> balance_pending (nano::account const &, bool only_confirmed);
	nano::uint128_t weight (nano::account const &);
	nano::uint128_t minimum_principal_weight ();
	void ongoing_bootstrap ();
	void backup_wallet ();
	void search_receivable_all ();
	void bootstrap_wallet ();
	bool collect_ledger_pruning_targets (std::deque<nano::block_hash> &, nano::account &, uint64_t const, uint64_t const, uint64_t const);
	void ledger_pruning (uint64_t const, bool);
	void ongoing_ledger_pruning ();
	int price (nano::uint128_t const &, int);
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (nano::work_version const) const;
	uint64_t default_receive_difficulty (nano::work_version const) const;
	uint64_t max_work_generate_difficulty (nano::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	std::optional<uint64_t> work_generate_blocking (nano::block &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (nano::work_version const, nano::root const &, uint64_t, std::optional<nano::account> const & = std::nullopt);
	void work_generate (nano::work_version const, nano::root const &, uint64_t, std::function<void (std::optional<uint64_t>)>, std::optional<nano::account> const & = std::nullopt, bool const = false);
	void add_initial_peers ();
	void start_election (std::shared_ptr<nano::block> const & block);
	bool block_confirmed (nano::block_hash const &);

	// This function may spuriously return false after returning true until the database transaction is refreshed
	bool block_confirmed_or_being_confirmed (nano::secure::transaction const &, nano::block_hash const &);
	bool block_confirmed_or_being_confirmed (nano::block_hash const &);

	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &);
	bool online () const;
	bool init_error () const;
	std::pair<uint64_t, std::unordered_map<nano::account, nano::uint128_t>> get_bootstrap_weights () const;
	/*
	 * Attempts to bootstrap block. This is the best effort, there is no guarantee that the block will be bootstrapped.
	 */
	void bootstrap_block (nano::block_hash const &);
	nano::account get_node_id () const;
	nano::telemetry_data local_telemetry () const;
	std::string identifier () const;

public:
	const nano::keypair node_id;
	nano::node_config config;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	nano::logger logger;
	std::unique_ptr<nano::thread_runner> runner_impl;
	nano::thread_runner & runner;
	boost::latch node_initialized_latch;
	nano::network_params & network_params;
	nano::stats stats;
	nano::thread_pool workers;
	nano::thread_pool bootstrap_workers;
	nano::thread_pool wallet_workers;
	nano::thread_pool election_workers;
	nano::node_flags flags;
	nano::work_pool & work;
	nano::distributed_work_factory distributed_work;
	std::unique_ptr<nano::store::component> store_impl;
	nano::store::component & store;
	nano::unchecked_map unchecked;
	std::unique_ptr<nano::wallets_store> wallets_store_impl;
	nano::wallets_store & wallets_store;
	std::unique_ptr<nano::ledger> ledger_impl;
	nano::ledger & ledger;
	nano::outbound_bandwidth_limiter outbound_limiter;
	std::unique_ptr<nano::message_processor> message_processor_impl;
	nano::message_processor & message_processor;
	nano::network network;
	nano::telemetry telemetry;
	nano::bootstrap_initiator bootstrap_initiator;
	nano::bootstrap_server bootstrap_server;
	std::unique_ptr<nano::transport::tcp_listener> tcp_listener_impl;
	nano::transport::tcp_listener & tcp_listener;
	std::filesystem::path application_path;
	nano::node_observers observers;
	std::unique_ptr<nano::port_mapping> port_mapping_impl;
	nano::port_mapping & port_mapping;
	nano::block_processor block_processor;
	std::unique_ptr<nano::confirming_set> confirming_set_impl;
	nano::confirming_set & confirming_set;
	std::unique_ptr<nano::active_elections> active_impl;
	nano::active_elections & active;
	std::unique_ptr<nano::online_reps> online_reps_impl;
	nano::online_reps & online_reps;
	nano::rep_crawler rep_crawler;
	nano::rep_tiers rep_tiers;
	unsigned warmed_up;
	std::unique_ptr<nano::local_vote_history> history_impl;
	nano::local_vote_history & history;
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer;
	nano::vote_cache vote_cache;
	std::unique_ptr<nano::vote_router> vote_router_impl;
	nano::vote_router & vote_router;
	std::unique_ptr<nano::vote_processor> vote_processor_impl;
	nano::vote_processor & vote_processor;
	std::unique_ptr<nano::vote_cache_processor> vote_cache_processor_impl;
	nano::vote_cache_processor & vote_cache_processor;
	std::unique_ptr<nano::vote_generator> generator_impl;
	nano::vote_generator & generator;
	std::unique_ptr<nano::vote_generator> final_generator_impl;
	nano::vote_generator & final_generator;
	std::unique_ptr<nano::scheduler::component> scheduler_impl;
	nano::scheduler::component & scheduler;
	std::unique_ptr<nano::request_aggregator> aggregator_impl;
	nano::request_aggregator & aggregator;
	nano::wallets wallets;
	nano::backlog_population backlog;
	std::unique_ptr<nano::bootstrap_ascending::service> ascendboot_impl;
	nano::bootstrap_ascending::service & ascendboot;
	nano::websocket_server websocket;
	nano::epoch_upgrader epoch_upgrader;
	std::unique_ptr<nano::local_block_broadcaster> local_block_broadcaster_impl;
	nano::local_block_broadcaster & local_block_broadcaster;
	nano::process_live_dispatcher process_live_dispatcher;
	std::unique_ptr<nano::peer_history> peer_history_impl;
	nano::peer_history & peer_history;
	std::unique_ptr<nano::monitor> monitor_impl;
	nano::monitor & monitor;

public:
	std::chrono::steady_clock::time_point const startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	// For tests only
	unsigned node_seq;
	// For tests only
	std::optional<uint64_t> work_generate_blocking (nano::block &);
	// For tests only
	std::optional<uint64_t> work_generate_blocking (nano::root const &, uint64_t);
	// For tests only
	std::optional<uint64_t> work_generate_blocking (nano::root const &);

public: // Testing convenience functions
	/**
		Creates a new write transaction and inserts `block' and returns result
		Transaction is comitted before function return
	 */
	[[nodiscard]] nano::block_status process (std::shared_ptr<nano::block> block);
	[[nodiscard]] nano::block_status process (secure::write_transaction const &, std::shared_ptr<nano::block> block);
	nano::block_hash latest (nano::account const &);
	nano::uint128_t balance (nano::account const &);

private:
	void long_inactivity_cleanup ();

	static std::string make_logger_identifier (nano::keypair const & node_id);
};

nano::keypair load_or_create_node_id (std::filesystem::path const & application_path);

std::unique_ptr<container_info_component> collect_container_info (node & node, std::string const & name);

nano::node_flags const & inactive_node_flag_defaults ();

}
