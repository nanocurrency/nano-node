#pragma once

#include <celerix/lib/config.hpp>
#include <celerix/lib/diagnosticsconfig.hpp>
#include <celerix/lib/errors.hpp>
#include <celerix/lib/lmdbconfig.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/rocksdbconfig.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/backlog_scan.hpp>
#include <celerix/node/block_processor.hpp>
#include <celerix/node/bootstrap/bootstrap_config.hpp>
#include <celerix/node/bootstrap/bootstrap_server.hpp>
#include <celerix/node/bounded_backlog.hpp>
#include <celerix/node/confirming_set.hpp>
#include <celerix/node/ipc/ipc_config.hpp>
#include <celerix/node/local_block_broadcaster.hpp>
#include <celerix/node/message_processor.hpp>
#include <celerix/node/monitor.hpp>
#include <celerix/node/network.hpp>
#include <celerix/node/peer_history.hpp>
#include <celerix/node/repcrawler.hpp>
#include <celerix/node/request_aggregator.hpp>
#include <celerix/node/scheduler/bucket.hpp>
#include <celerix/node/scheduler/hinted.hpp>
#include <celerix/node/scheduler/optimistic.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/transport/tcp_config.hpp>
#include <celerix/node/transport/tcp_listener.hpp>
#include <celerix/node/vote_cache.hpp>
#include <celerix/node/vote_processor.hpp>
#include <celerix/node/websocketconfig.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/generate_cache_flags.hpp>

#include <chrono>
#include <optional>
#include <vector>

namespace celerix
{
class tomlconfig;

/**
 * Node configuration
 */
class node_config
{
public:
	// TODO: Users of this class rely on the default copy consturctor. This prevents using unique_ptrs with forward declared types.
	node_config (celerix::network_params & network_params = celerix::dev::network_params);
	node_config (const std::optional<uint16_t> &, celerix::network_params & network_params = celerix::dev::network_params);
	~node_config ();

	celerix::error serialize_toml (celerix::tomlconfig &) const;
	celerix::error deserialize_toml (celerix::tomlconfig &);

	bool upgrade_json (unsigned, celerix::jsonconfig &);
	celerix::account random_representative () const;

	celerix::network_params network_params;
	std::optional<uint16_t> peering_port{};
	celerix::scheduler::optimistic_config optimistic_scheduler;
	celerix::scheduler::hinted_config hinted_scheduler;
	celerix::scheduler::priority_config priority_scheduler;
	celerix::scheduler::priority_bucket_config priority_bucket;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::pair<std::string, uint16_t>> secondary_work_peers{ { "127.0.0.1", 8076 } }; /* Default of celerix-pow-server */
	std::vector<std::string> preconfigured_peers;
	std::vector<celerix::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	celerix::amount receive_minimum{ celerix::celerix_ratio / 1000 / 1000 }; // 0.000001 celerix
	celerix::amount vote_minimum{ celerix::Kcelerix_ratio }; // 1000 celerix
	celerix::amount rep_crawler_weight_minimum{ "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" };
	std::chrono::milliseconds vote_generator_delay{ std::chrono::milliseconds (100) };
	celerix::amount online_weight_minimum{ 60000 * celerix::Kcelerix_ratio }; // 60 million celerix
	/*
	 * The minimum vote weight that a representative must have for its vote to be counted.
	 * All representatives above this weight will be kept in memory!
	 */
	celerix::amount representative_vote_weight_minimum{ 10 * celerix::celerix_ratio };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ env_io_threads ().value_or (std::max (4u, celerix::hardware_concurrency ())) };
	unsigned network_threads{ std::max (4u, celerix::hardware_concurrency ()) };
	unsigned work_threads{ std::max (4u, celerix::hardware_concurrency ()) };
	unsigned background_threads{ std::max (4u, celerix::hardware_concurrency ()) };
	/* Use half available threads on the system for signature checking. The calling thread does checks as well, so these are extra worker threads */
	unsigned signature_checker_threads{ std::max (2u, celerix::hardware_concurrency () / 2) };
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	unsigned bootstrap_initiator_threads{ 1 };
	unsigned bootstrap_serving_threads{ 1 };
	uint32_t bootstrap_frontier_request_count{ 1024 * 1024 };
	celerix::websocket::config websocket_config;
	celerix::diagnostics_config diagnostics_config;
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	bool allow_local_peers{ !(network_params.network.is_live_network () || network_params.network.is_test_network ()) }; // disable by default for live network
	celerix::stats_config stats_config;
	celerix::ipc::ipc_config ipc_config;
	std::string external_address;
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (500) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) }; // 4 hours
	/** Timeout for initiated async operations */
	std::chrono::seconds tcp_io_timeout{ (network_params.network.is_dev_network () && !is_sanitizer_build ()) ? std::chrono::seconds (5) : std::chrono::seconds (15) };
	std::chrono::celerixseconds pow_sleep_interval{ 0 };

	bool use_memory_pools{ true };
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	/** Default outbound traffic shaping is 10MB/s */
	std::size_t bandwidth_limit{ 10 * 1024 * 1024 };
	/** By default, allow bursts of 15MB/s (not sustainable) */
	double bandwidth_limit_burst_ratio{ 3. };
	/** Default bootstrap outbound traffic limit is 5MB/s */
	std::size_t bootstrap_bandwidth_limit{ 5 * 1024 * 1024 };
	/** Bootstrap traffic does not need bursts */
	double bootstrap_bandwidth_burst_ratio{ 1. };
	celerix::bootstrap_config bootstrap;
	celerix::bootstrap_server_config bootstrap_server;
	std::chrono::milliseconds confirming_set_batch_time{ 250 };
	bool backup_before_upgrade{ false };
	double max_work_generate_multiplier{ 64. };
	uint32_t max_queued_requests{ 512 };
	unsigned request_aggregator_threads{ std::min (celerix::hardware_concurrency (), 4u) }; // Max 4 threads if available
	unsigned max_unchecked_blocks{ 65536 };
	std::size_t max_backlog{ 100000 };
	std::chrono::seconds max_pruning_age{ !network_params.network.is_beta_network () ? std::chrono::seconds (24 * 60 * 60) : std::chrono::seconds (5 * 60) }; // 1 day; 5 minutes for beta network
	uint64_t max_pruning_depth{ 0 };
	celerix::rocksdb_config rocksdb_config;
	celerix::lmdb_config lmdb_config;
	bool enable_upnp{ true };

public:
	celerix::vote_cache_config vote_cache;
	celerix::rep_crawler_config rep_crawler;
	celerix::block_processor_config block_processor;
	celerix::active_elections_config active_elections;
	celerix::vote_processor_config vote_processor;
	celerix::peer_history_config peer_history;
	celerix::transport::tcp_config tcp;
	celerix::request_aggregator_config request_aggregator;
	celerix::message_processor_config message_processor;
	celerix::network_config network;
	celerix::local_block_broadcaster_config local_block_broadcaster;
	celerix::confirming_set_config confirming_set;
	celerix::monitor_config monitor;
	celerix::backlog_scan_config backlog_scan;
	celerix::bounded_backlog_config bounded_backlog;

public:
	/** Entry is ignored if it cannot be parsed as a valid address:port */
	void deserialize_address (std::string const &, std::vector<std::pair<std::string, uint16_t>> &) const;

private:
	static std::optional<unsigned> env_io_threads ();
};

class node_flags final
{
public:
	std::vector<std::string> config_overrides;
	std::vector<std::string> rpc_config_overrides;
	bool disable_add_initial_peers{ false }; // For testing only
	bool disable_activate_successors{ false }; // For testing only
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_bootstrap_bulk_pull_server{ false };
	bool disable_bootstrap_bulk_push_client{ false };
	bool disable_ongoing_bootstrap{ false }; // For testing only
	bool disable_rep_crawler{ false };
	bool disable_request_loop{ false }; // For testing only
	bool disable_tcp_realtime{ false };
	bool disable_providing_telemetry_metrics{ false };
	bool disable_block_processor_unchecked_deletion{ false };
	bool disable_block_processor_republishing{ false };
	bool allow_bootstrap_peers_duplicates{ false };
	bool disable_max_peers_per_ip{ false }; // For testing only
	bool disable_max_peers_per_subnetwork{ false }; // For testing only
	bool disable_search_pending{ false }; // For testing only
	bool enable_pruning{ false };
	bool fast_bootstrap{ false };
	bool read_only{ false };
	bool disable_connection_cleanup{ false };
	celerix::generate_cache_flags generate_cache;
	bool inactive_node{ false };
	std::size_t block_processor_batch_size{ 0 };
	std::size_t block_processor_full_size{ 65536 };
	std::size_t block_processor_verification_size{ 0 };
	std::size_t vote_processor_capacity{ 144 * 1024 };
	std::size_t bootstrap_interval{ 0 }; // For testing only
};
}
