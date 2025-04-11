#pragma once

#include <nano/lib/block_uniquer.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/distributed_work_factory.hpp>
#include <nano/node/epoch_upgrader.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/portmapping.hpp>
#include <nano/node/rep_tiers.hpp>
#include <nano/node/repcrawler.hpp>
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
class node final : public std::enable_shared_from_this<node>
{
public:
	node (std::shared_ptr<boost::asio::io_context>, uint16_t peering_port, std::filesystem::path const & application_path, nano::work_pool &, nano::node_flags = nano::node_flags (), unsigned seq = 0);
	node (std::shared_ptr<boost::asio::io_context>, std::filesystem::path const & application_path, nano::node_config const &, nano::work_pool &, nano::node_flags = nano::node_flags (), unsigned seq = 0);
	~node ();

public:
	void start ();
	void stop ();

	std::shared_ptr<nano::node> shared ();

	bool copy_with_compaction (std::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	int store_version ();
	void inbound (nano::message const &, std::shared_ptr<nano::transport::channel> const &);
	void process_active (std::shared_ptr<nano::block> const &);
	std::optional<nano::block_status> process_local (std::shared_ptr<nano::block> const &);
	void process_local_async (std::shared_ptr<nano::block> const &);
	void keepalive_preconfigured ();
	std::shared_ptr<nano::block> block (nano::block_hash const &);
	bool block_or_pruned_exists (nano::block_hash const &) const;
	std::pair<nano::uint128_t, nano::uint128_t> balance_pending (nano::account const &, bool only_confirmed);
	nano::uint128_t weight (nano::account const &);
	nano::uint128_t minimum_principal_weight ();
	void backup_wallet ();
	void search_receivable_all ();
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

	uint64_t block_count () const;
	uint64_t cemented_count () const;

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
	nano::container_info container_info () const;

public:
	const std::filesystem::path application_path;
	const nano::keypair node_id;
	boost::latch node_initialized_latch;
	nano::node_config config;
	nano::node_flags flags;
	nano::network_params & network_params;
	std::shared_ptr<boost::asio::io_context> io_ctx_shared;
	boost::asio::io_context & io_ctx;
	std::unique_ptr<nano::logger> logger_impl;
	nano::logger & logger;
	std::unique_ptr<nano::stats> stats_impl;
	nano::stats & stats;
	std::unique_ptr<nano::thread_runner> runner_impl;
	nano::thread_runner & runner;
	std::unique_ptr<nano::node_observers> observers_impl;
	nano::node_observers & observers;
	std::unique_ptr<nano::thread_pool> workers_impl;
	nano::thread_pool & workers;
	std::unique_ptr<nano::thread_pool> bootstrap_workers_impl;
	nano::thread_pool & bootstrap_workers;
	std::unique_ptr<nano::thread_pool> wallet_workers_impl;
	nano::thread_pool & wallet_workers;
	std::unique_ptr<nano::thread_pool> election_workers_impl;
	nano::thread_pool & election_workers;
	nano::work_pool & work;
	std::unique_ptr<nano::distributed_work_factory> distributed_work_impl;
	nano::distributed_work_factory & distributed_work;
	std::unique_ptr<nano::store::component> store_impl;
	nano::store::component & store;
	std::unique_ptr<nano::unchecked_map> unchecked_impl;
	nano::unchecked_map & unchecked;
	std::unique_ptr<nano::wallets_store> wallets_store_impl;
	nano::wallets_store & wallets_store;
	std::unique_ptr<nano::wallets> wallets_impl;
	nano::wallets & wallets;
	std::unique_ptr<nano::ledger> ledger_impl;
	nano::ledger & ledger;
	std::unique_ptr<nano::ledger_notifications> ledger_notifications_impl;
	nano::ledger_notifications & ledger_notifications;
	std::unique_ptr<nano::bandwidth_limiter> outbound_limiter_impl;
	nano::bandwidth_limiter & outbound_limiter;
	std::unique_ptr<nano::message_processor> message_processor_impl;
	nano::message_processor & message_processor;
	std::unique_ptr<nano::network> network_impl;
	nano::network & network;
	std::shared_ptr<nano::transport::channel> loopback_channel;
	std::unique_ptr<nano::telemetry> telemetry_impl;
	nano::telemetry & telemetry;
	std::unique_ptr<nano::transport::tcp_listener> tcp_listener_impl;
	nano::transport::tcp_listener & tcp_listener;
	std::unique_ptr<nano::port_mapping> port_mapping_impl;
	nano::port_mapping & port_mapping;
	std::unique_ptr<nano::block_processor> block_processor_impl;
	nano::block_processor & block_processor;
	std::unique_ptr<nano::fork_cache> fork_cache_impl;
	nano::fork_cache & fork_cache;
	std::unique_ptr<nano::cementing_set> cementing_set_impl;
	nano::cementing_set & cementing_set;
	std::unique_ptr<nano::bucketing> bucketing_impl;
	nano::bucketing & bucketing;
	std::unique_ptr<nano::active_elections> active_impl;
	nano::active_elections & active;
	std::unique_ptr<nano::online_reps> online_reps_impl;
	nano::online_reps & online_reps;
	std::unique_ptr<nano::rep_crawler> rep_crawler_impl;
	nano::rep_crawler & rep_crawler;
	std::unique_ptr<nano::rep_tiers> rep_tiers_impl;
	nano::rep_tiers & rep_tiers;
	std::unique_ptr<nano::local_vote_history> history_impl;
	nano::local_vote_history & history;
	std::unique_ptr<nano::block_uniquer> block_uniquer_impl;
	nano::block_uniquer & block_uniquer;
	std::unique_ptr<nano::vote_uniquer> vote_uniquer_impl;
	nano::vote_uniquer & vote_uniquer;
	std::unique_ptr<nano::vote_cache> vote_cache_impl;
	nano::vote_cache & vote_cache;
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
	std::unique_ptr<nano::backlog_scan> backlog_scan_impl;
	nano::backlog_scan & backlog_scan;
	std::unique_ptr<nano::bounded_backlog> backlog_impl;
	nano::bounded_backlog & backlog;
	std::unique_ptr<nano::bootstrap_server> bootstrap_server_impl;
	nano::bootstrap_server & bootstrap_server;
	std::unique_ptr<nano::bootstrap_service> bootstrap_impl;
	nano::bootstrap_service & bootstrap;
	std::unique_ptr<nano::websocket_server> websocket_impl;
	nano::websocket_server & websocket;
	std::unique_ptr<nano::epoch_upgrader> epoch_upgrader_impl;
	nano::epoch_upgrader & epoch_upgrader;
	std::unique_ptr<nano::local_block_broadcaster> local_block_broadcaster_impl;
	nano::local_block_broadcaster & local_block_broadcaster;
	std::unique_ptr<nano::peer_history> peer_history_impl;
	nano::peer_history & peer_history;
	std::unique_ptr<nano::monitor> monitor_impl;
	nano::monitor & monitor;
	std::unique_ptr<nano::http_callbacks> http_callbacks_impl;
	nano::http_callbacks & http_callbacks;
	std::unique_ptr<nano::pruning> pruning_impl;
	nano::pruning & pruning;
	std::unique_ptr<nano::vote_rebroadcaster> vote_rebroadcaster_impl;
	nano::vote_rebroadcaster & vote_rebroadcaster;

public:
	std::chrono::steady_clock::time_point const startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;

public: // For tests only
	const unsigned node_seq;
	std::optional<uint64_t> work_generate_blocking (nano::block &);
	std::optional<uint64_t> work_generate_blocking (nano::root const &, uint64_t);
	std::optional<uint64_t> work_generate_blocking (nano::root const &);

public: // Testing convenience functions
	[[nodiscard]] nano::block_status process (std::shared_ptr<nano::block> block);
	[[nodiscard]] nano::block_status process (secure::write_transaction const &, std::shared_ptr<nano::block> block);
	nano::block_hash latest (nano::account const &);
	nano::uint128_t balance (nano::account const &);

private:
	static std::string make_logger_identifier (nano::keypair const & node_id);
};

nano::keypair load_or_create_node_id (std::filesystem::path const & application_path);

nano::node_flags const & inactive_node_flag_defaults ();

}
