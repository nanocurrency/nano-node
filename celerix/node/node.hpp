#pragma once

#include <celerix/lib/block_uniquer.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/work.hpp>
#include <celerix/node/distributed_work_factory.hpp>
#include <celerix/node/epoch_upgrader.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/node_observers.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/portmapping.hpp>
#include <celerix/node/process_live_dispatcher.hpp>
#include <celerix/node/rep_tiers.hpp>
#include <celerix/node/repcrawler.hpp>
#include <celerix/node/transport/tcp_server.hpp>
#include <celerix/node/unchecked_map.hpp>
#include <celerix/node/vote_cache.hpp>
#include <celerix/node/wallet.hpp>
#include <celerix/node/websocket.hpp>
#include <celerix/secure/utility.hpp>

#include <boost/program_options.hpp>
#include <boost/thread/latch.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace celerix
{
class node final : public std::enable_shared_from_this<node>
{
public:
	node (std::shared_ptr<boost::asio::io_context>, uint16_t peering_port, std::filesystem::path const & application_path, celerix::work_pool &, celerix::node_flags = celerix::node_flags (), unsigned seq = 0);
	node (std::shared_ptr<boost::asio::io_context>, std::filesystem::path const & application_path, celerix::node_config const &, celerix::work_pool &, celerix::node_flags = celerix::node_flags (), unsigned seq = 0);
	~node ();

public:
	void start ();
	void stop ();

	std::shared_ptr<celerix::node> shared ();

	bool copy_with_compaction (std::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	int store_version ();
	void inbound (celerix::message const &, std::shared_ptr<celerix::transport::channel> const &);
	void process_active (std::shared_ptr<celerix::block> const &);
	std::optional<celerix::block_status> process_local (std::shared_ptr<celerix::block> const &);
	void process_local_async (std::shared_ptr<celerix::block> const &);
	void keepalive_preconfigured ();
	std::shared_ptr<celerix::block> block (celerix::block_hash const &);
	bool block_or_pruned_exists (celerix::block_hash const &) const;
	std::pair<celerix::uint128_t, celerix::uint128_t> balance_pending (celerix::account const &, bool only_confirmed);
	celerix::uint128_t weight (celerix::account const &);
	celerix::uint128_t minimum_principal_weight ();
	void backup_wallet ();
	void search_receivable_all ();
	bool collect_ledger_pruning_targets (std::deque<celerix::block_hash> &, celerix::account &, uint64_t const, uint64_t const, uint64_t const);
	void ledger_pruning (uint64_t const, bool);
	void ongoing_ledger_pruning ();
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (celerix::work_version const) const;
	uint64_t default_receive_difficulty (celerix::work_version const) const;
	uint64_t max_work_generate_difficulty (celerix::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	std::optional<uint64_t> work_generate_blocking (celerix::block &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (celerix::work_version const, celerix::root const &, uint64_t, std::optional<celerix::account> const & = std::nullopt);
	void work_generate (celerix::work_version const, celerix::root const &, uint64_t, std::function<void (std::optional<uint64_t>)>, std::optional<celerix::account> const & = std::nullopt, bool const = false);
	void add_initial_peers ();
	void start_election (std::shared_ptr<celerix::block> const & block);
	bool block_confirmed (celerix::block_hash const &);

	// This function may spuriously return false after returning true until the database transaction is refreshed
	bool block_confirmed_or_being_confirmed (celerix::secure::transaction const &, celerix::block_hash const &);
	bool block_confirmed_or_being_confirmed (celerix::block_hash const &);

	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &);
	bool online () const;
	bool init_error () const;
	std::pair<uint64_t, std::unordered_map<celerix::account, celerix::uint128_t>> get_bootstrap_weights () const;
	/*
	 * Attempts to bootstrap block. This is the best effort, there is no guarantee that the block will be bootstrapped.
	 */
	void bootstrap_block (celerix::block_hash const &);
	celerix::account get_node_id () const;
	celerix::telemetry_data local_telemetry () const;
	std::string identifier () const;
	celerix::container_info container_info () const;

public:
	const std::filesystem::path application_path;
	const celerix::keypair node_id;
	boost::latch node_initialized_latch;
	celerix::node_config config;
	celerix::node_flags flags;
	celerix::network_params & network_params;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	std::unique_ptr<celerix::logger> logger_impl;
	celerix::logger & logger;
	std::unique_ptr<celerix::stats> stats_impl;
	celerix::stats & stats;
	std::unique_ptr<celerix::thread_runner> runner_impl;
	celerix::thread_runner & runner;
	std::unique_ptr<celerix::node_observers> observers_impl;
	celerix::node_observers & observers;
	std::unique_ptr<celerix::thread_pool> workers_impl;
	celerix::thread_pool & workers;
	std::unique_ptr<celerix::thread_pool> bootstrap_workers_impl;
	celerix::thread_pool & bootstrap_workers;
	std::unique_ptr<celerix::thread_pool> wallet_workers_impl;
	celerix::thread_pool & wallet_workers;
	std::unique_ptr<celerix::thread_pool> election_workers_impl;
	celerix::thread_pool & election_workers;
	celerix::work_pool & work;
	std::unique_ptr<celerix::distributed_work_factory> distributed_work_impl;
	celerix::distributed_work_factory & distributed_work;
	std::unique_ptr<celerix::store::component> store_impl;
	celerix::store::component & store;
	std::unique_ptr<celerix::unchecked_map> unchecked_impl;
	celerix::unchecked_map & unchecked;
	std::unique_ptr<celerix::wallets_store> wallets_store_impl;
	celerix::wallets_store & wallets_store;
	std::unique_ptr<celerix::wallets> wallets_impl;
	celerix::wallets & wallets;
	std::unique_ptr<celerix::ledger> ledger_impl;
	celerix::ledger & ledger;
	std::unique_ptr<celerix::bandwidth_limiter> outbound_limiter_impl;
	celerix::bandwidth_limiter & outbound_limiter;
	std::unique_ptr<celerix::message_processor> message_processor_impl;
	celerix::message_processor & message_processor;
	std::unique_ptr<celerix::network> network_impl;
	celerix::network & network;
	std::unique_ptr<celerix::telemetry> telemetry_impl;
	celerix::telemetry & telemetry;
	std::unique_ptr<celerix::transport::tcp_listener> tcp_listener_impl;
	celerix::transport::tcp_listener & tcp_listener;
	std::unique_ptr<celerix::port_mapping> port_mapping_impl;
	celerix::port_mapping & port_mapping;
	std::unique_ptr<celerix::block_processor> block_processor_impl;
	celerix::block_processor & block_processor;
	std::unique_ptr<celerix::confirming_set> confirming_set_impl;
	celerix::confirming_set & confirming_set;
	std::unique_ptr<celerix::bucketing> bucketing_impl;
	celerix::bucketing & bucketing;
	std::unique_ptr<celerix::active_elections> active_impl;
	celerix::active_elections & active;
	std::unique_ptr<celerix::online_reps> online_reps_impl;
	celerix::online_reps & online_reps;
	std::unique_ptr<celerix::rep_crawler> rep_crawler_impl;
	celerix::rep_crawler & rep_crawler;
	std::unique_ptr<celerix::rep_tiers> rep_tiers_impl;
	celerix::rep_tiers & rep_tiers;
	std::unique_ptr<celerix::local_vote_history> history_impl;
	celerix::local_vote_history & history;
	std::unique_ptr<celerix::block_uniquer> block_uniquer_impl;
	celerix::block_uniquer & block_uniquer;
	std::unique_ptr<celerix::vote_uniquer> vote_uniquer_impl;
	celerix::vote_uniquer & vote_uniquer;
	std::unique_ptr<celerix::vote_cache> vote_cache_impl;
	celerix::vote_cache & vote_cache;
	std::unique_ptr<celerix::vote_router> vote_router_impl;
	celerix::vote_router & vote_router;
	std::unique_ptr<celerix::vote_processor> vote_processor_impl;
	celerix::vote_processor & vote_processor;
	std::unique_ptr<celerix::vote_cache_processor> vote_cache_processor_impl;
	celerix::vote_cache_processor & vote_cache_processor;
	std::unique_ptr<celerix::vote_generator> generator_impl;
	celerix::vote_generator & generator;
	std::unique_ptr<celerix::vote_generator> final_generator_impl;
	celerix::vote_generator & final_generator;
	std::unique_ptr<celerix::scheduler::component> scheduler_impl;
	celerix::scheduler::component & scheduler;
	std::unique_ptr<celerix::request_aggregator> aggregator_impl;
	celerix::request_aggregator & aggregator;
	std::unique_ptr<celerix::backlog_scan> backlog_scan_impl;
	celerix::backlog_scan & backlog_scan;
	std::unique_ptr<celerix::bounded_backlog> backlog_impl;
	celerix::bounded_backlog & backlog;
	std::unique_ptr<celerix::bootstrap_server> bootstrap_server_impl;
	celerix::bootstrap_server & bootstrap_server;
	std::unique_ptr<celerix::bootstrap_service> bootstrap_impl;
	celerix::bootstrap_service & bootstrap;
	std::unique_ptr<celerix::websocket_server> websocket_impl;
	celerix::websocket_server & websocket;
	std::unique_ptr<celerix::epoch_upgrader> epoch_upgrader_impl;
	celerix::epoch_upgrader & epoch_upgrader;
	std::unique_ptr<celerix::local_block_broadcaster> local_block_broadcaster_impl;
	celerix::local_block_broadcaster & local_block_broadcaster;
	std::unique_ptr<celerix::process_live_dispatcher> process_live_dispatcher_impl;
	celerix::process_live_dispatcher & process_live_dispatcher;
	std::unique_ptr<celerix::peer_history> peer_history_impl;
	celerix::peer_history & peer_history;
	std::unique_ptr<celerix::monitor> monitor_impl;
	celerix::monitor & monitor;

public:
	std::chrono::steady_clock::time_point const startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;

public: // For tests only
	const unsigned node_seq;
	std::optional<uint64_t> work_generate_blocking (celerix::block &);
	std::optional<uint64_t> work_generate_blocking (celerix::root const &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (celerix::root const &);

public: // Testing convenience functions
	[[nodiscard]] celerix::block_status process (std::shared_ptr<celerix::block> block);
	[[nodiscard]] celerix::block_status process (secure::write_transaction const &, std::shared_ptr<celerix::block> block);
	celerix::block_hash latest (celerix::account const &);
	celerix::uint128_t balance (celerix::account const &);

private:
	static std::string make_logger_identifier (celerix::keypair const & node_id);
};

celerix::keypair load_or_create_node_id (std::filesystem::path const & application_path);

celerix::node_flags const & inactive_node_flag_defaults ();

}
