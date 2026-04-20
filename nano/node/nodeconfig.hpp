#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/indirect.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/ratios.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/fwd.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/generate_cache_flags.hpp>
#include <nano/secure/network_params.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace nano
{
class tomlconfig;

class node_config
{
public:
	node_config (nano::network_params const & = nano::dev::network_params);
	node_config (node_config const &);
	node_config (node_config &&) noexcept;
	~node_config ();

	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);

	nano::account random_representative () const;

public:
	nano::network_params network_params;

	std::optional<uint16_t> peering_port{};
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::pair<std::string, uint16_t>> secondary_work_peers{ { "127.0.0.1", 8076 } }; // Default of nano-pow-server
	std::vector<std::string> preconfigured_peers;
	std::vector<nano::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	nano::amount receive_minimum{ nano::nano_ratio / 1000 / 1000 }; // 0.000001 nano
	nano::amount vote_minimum{ nano::Knano_ratio }; // 1000 nano
	nano::amount rep_crawler_weight_minimum{ "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" };
	nano::amount online_weight_minimum{ 60000 * nano::Knano_ratio }; // 60 million nano
	/**
	 * The minimum vote weight that a representative must have for its vote to be counted.
	 * All representatives above this weight will be kept in memory! */
	nano::amount representative_vote_weight_minimum{ 10 * nano::nano_ratio };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ env_io_threads ().value_or (std::max (4u, nano::hardware_concurrency ())) };
	unsigned network_threads{ std::max (4u, nano::hardware_concurrency ()) };
	unsigned work_threads{ std::max (4u, nano::hardware_concurrency ()) };
	unsigned background_threads{ std::max (4u, nano::hardware_concurrency ()) };
	unsigned wallet_threads{ 4 };
	unsigned signature_checker_threads{ std::max (2u, nano::hardware_concurrency () / 2) };
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	unsigned bootstrap_initiator_threads{ 1 };
	unsigned bootstrap_serving_threads{ 1 };
	uint32_t bootstrap_frontier_request_count{ 1024 * 1024 };
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	bool allow_local_peers{ !(network_params.network.is_live_network () || network_params.network.is_test_network ()) };
	std::string external_address;
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (500) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) };
	std::chrono::nanoseconds pow_sleep_interval{ 0 };
	bool use_memory_pools{ true };
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	/** Default outbound traffic shaping is 10MB/s */
	std::size_t bandwidth_limit{ 10 * 1024 * 1024 };
	double bandwidth_limit_burst_ratio{ 3. };
	/** Default bootstrap outbound traffic limit is 5MB/s */
	std::size_t bootstrap_bandwidth_limit{ 5 * 1024 * 1024 };
	double bootstrap_bandwidth_burst_ratio{ 1. };
	bool backup_before_upgrade{ false };
	double max_work_generate_multiplier{ 64. };
	uint32_t max_queued_requests{ 512 };
	unsigned max_unchecked_blocks{ 65536 };
	std::size_t max_backlog{ 100000 };
	std::chrono::seconds max_pruning_age{ !network_params.network.is_beta_network () ? std::chrono::seconds (24 * 60 * 60) : std::chrono::seconds (5 * 60) }; // 1 day; 5 minutes for beta network
	uint64_t max_pruning_depth{ 0 };
	nano::database_backend database_backend{ nano::default_database_backend () };
	bool enable_upnp{ true };
	std::size_t max_ledger_notifications{ 300 };

public: // Subsystem configs
	nano::indirect<nano::scheduler::optimistic_config> optimistic_scheduler;
	nano::indirect<nano::scheduler::hinted_config> hinted_scheduler;
	nano::indirect<nano::scheduler::priority_config> priority_scheduler;
	nano::indirect<nano::websocket::config> websocket_config;
	nano::indirect<nano::store::txn_tracking_config> txn_tracking;
	nano::indirect<nano::stats_config> stats_config;
	nano::indirect<nano::ipc::ipc_config> ipc_config;
	nano::indirect<nano::bootstrap_config> bootstrap;
	nano::indirect<nano::bootstrap_server_config> bootstrap_server;
	nano::indirect<nano::rocksdb_config> rocksdb_config;
	nano::indirect<nano::lmdb_config> lmdb_config;
	nano::indirect<nano::vote_cache_config> vote_cache;
	nano::indirect<nano::rep_crawler_config> rep_crawler;
	nano::indirect<nano::block_processor_config> block_processor;
	nano::indirect<nano::active_elections_config> active_elections;
	nano::indirect<nano::vote_generator_config> vote_generator;
	nano::indirect<nano::vote_processor_config> vote_processor;
	nano::indirect<nano::peer_history_config> peer_history;
	nano::indirect<nano::transport::tcp_config> tcp;
	nano::indirect<nano::vote_replier_config> vote_replier;
	nano::indirect<nano::message_processor_config> message_processor;
	nano::indirect<nano::network_config> network;
	nano::indirect<nano::local_block_broadcaster_config> local_block_broadcaster;
	nano::indirect<nano::cementing_set_config> cementing_set;
	nano::indirect<nano::monitor_config> monitor;
	nano::indirect<nano::backlog_scan_config> backlog_scan;
	nano::indirect<nano::bounded_backlog_config> bounded_backlog;
	nano::indirect<nano::vote_rebroadcaster_config> vote_rebroadcaster;
	nano::indirect<nano::block_rebroadcaster_config> block_rebroadcaster;
	nano::indirect<nano::fork_cache_config> fork_cache;

public:
	/** Entry is ignored if it cannot be parsed as a valid address:port */
	static void deserialize_address (std::string const &, std::vector<std::pair<std::string, uint16_t>> &);
	static std::optional<unsigned> env_io_threads ();
};

class node_flags final
{
public:
	std::vector<std::string> config_overrides;
	std::vector<std::string> rpc_config_overrides;
	bool disable_add_initial_peers{ false };
	bool disable_activate_successors{ false };
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_bootstrap_bulk_pull_server{ false };
	bool disable_bootstrap_bulk_push_client{ false };
	bool disable_ongoing_bootstrap{ false };
	bool disable_reachout{ false };
	bool disable_reachout_preconfigured{ false };
	bool disable_rep_crawler{ false };
	bool disable_request_loop{ false };
	bool disable_tcp_realtime{ false };
	bool disable_providing_telemetry_metrics{ false };
	bool disable_block_processor_unchecked_deletion{ false };
	bool allow_bootstrap_peers_duplicates{ false };
	bool disable_max_peers_per_ip{ false };
	bool disable_max_peers_per_subnetwork{ false };
	bool disable_search_pending{ false };
	bool enable_pruning{ false };
	bool enable_rpc{ false };
	bool enable_voting{ false };
	bool super_rebroadcaster{ false };
	bool fast_bootstrap{ false };
	std::optional<std::filesystem::path> runtime_info_file; // Write runtime_info.json with ports
	bool read_only{ false };
	bool disable_connection_cleanup{ false };
	nano::generate_cache_flags generate_cache;
	bool inactive_node{ false };
	std::size_t block_processor_batch_size{ 0 };
	std::size_t block_processor_full_size{ 65536 };
	std::size_t block_processor_verification_size{ 0 };
	std::size_t vote_processor_capacity{ 144 * 1024 };
	std::size_t bootstrap_interval{ 0 };
	std::optional<nano::node_capabilities_flags> capabilities_override;
};
}
