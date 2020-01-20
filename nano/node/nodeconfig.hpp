#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/ipcconfig.hpp>
#include <nano/node/logging.hpp>
#include <nano/node/websocketconfig.hpp>
#include <nano/secure/common.hpp>

#include <chrono>
#include <vector>

namespace nano
{
class tomlconfig;

enum class frontiers_confirmation_mode : uint8_t
{
	always, // Always confirm frontiers
	automatic, // Always mode if node contains representative with at least 50% of principal weight, less frequest requests if not
	disabled, // Do not confirm frontiers
	invalid
};

/**
 * Node configuration
 */
class node_config
{
public:
	node_config ();
	node_config (uint16_t, nano::logging const &);
	nano::error serialize_json (nano::jsonconfig &) const;
	nano::error deserialize_json (bool &, nano::jsonconfig &);
	nano::error serialize_toml (nano::tomlconfig &) const;
	nano::error deserialize_toml (nano::tomlconfig &);
	bool upgrade_json (unsigned, nano::jsonconfig &);
	nano::account random_representative () const;
	nano::network_params network_params;
	uint16_t peering_port{ 0 };
	nano::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::pair<std::string, uint16_t>> secondary_work_peers{ { "127.0.0.1", 8076 } }; /* Default of nano-pow-server */
	std::vector<std::string> preconfigured_peers;
	std::vector<nano::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	nano::amount receive_minimum{ nano::xrb_ratio };
	nano::amount vote_minimum{ nano::Gxrb_ratio };
	std::chrono::milliseconds vote_generator_delay{ std::chrono::milliseconds (100) };
	unsigned vote_generator_threshold{ 3 };
	nano::amount online_weight_minimum{ 60000 * nano::Gxrb_ratio };
	unsigned online_weight_quorum{ 50 };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	unsigned network_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	unsigned work_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	unsigned signature_checker_threads{ (std::thread::hardware_concurrency () != 0) ? std::thread::hardware_concurrency () - 1 : 0 }; /* The calling thread does checks as well so remove it from the number of threads used */
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	nano::websocket::config websocket_config;
	nano::diagnostics_config diagnostics_config;
	size_t confirmation_history_size{ 2048 };
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	int lmdb_max_dbs{ 128 };
	bool allow_local_peers{ !network_params.network.is_live_network () }; // disable by default for live network
	nano::stat_config stat_config;
	nano::ipc::ipc_config ipc_config;
	std::string external_address;
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (5000) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) }; // 4 hours
	/** Timeout for initiated async operations */
	std::chrono::seconds tcp_io_timeout{ (network_params.network.is_test_network () && !is_sanitizer_build) ? std::chrono::seconds (5) : std::chrono::seconds (15) };
	std::chrono::nanoseconds pow_sleep_interval{ 0 };
	size_t active_elections_size{ 10000 };
	/** Default maximum incoming TCP connections, including realtime network & bootstrap */
	unsigned tcp_incoming_connections_max{ 1024 };
	bool use_memory_pools{ true };
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	size_t bandwidth_limit{ 5 * 1024 * 1024 }; // 5MB/s
	std::chrono::milliseconds conf_height_processor_batch_min_time{ 50 };
	bool backup_before_upgrade{ false };
	std::chrono::seconds work_watcher_period{ std::chrono::seconds (5) };
	double max_work_generate_multiplier{ 64. };
	uint64_t max_work_generate_difficulty{ nano::network_constants::publish_full_threshold };
	nano::rocksdb_config rocksdb_config;
	nano::frontiers_confirmation_mode frontiers_confirmation{ nano::frontiers_confirmation_mode::automatic };
	std::string serialize_frontiers_confirmation (nano::frontiers_confirmation_mode) const;
	nano::frontiers_confirmation_mode deserialize_frontiers_confirmation (std::string const &);
	/** Entry is ignored if it cannot be parsed as a valid address:port */
	void deserialize_address (std::string const &, std::vector<std::pair<std::string, uint16_t>> &) const;

	static unsigned json_version ()
	{
		return 18;
	}
};

class node_flags final
{
public:
	std::vector<std::string> config_overrides;
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_bootstrap_bulk_pull_server{ false };
	bool disable_bootstrap_bulk_push_client{ false };
	bool disable_rep_crawler{ false };
	bool disable_request_loop{ false };
	bool disable_tcp_realtime{ false };
	bool disable_udp{ false };
	bool disable_unchecked_cleanup{ false };
	bool disable_unchecked_drop{ true };
	bool fast_bootstrap{ false };
	bool read_only{ false };
	nano::generate_cache generate_cache;
	bool inactive_node{ false };
	size_t sideband_batch_size{ 512 };
	size_t block_processor_batch_size{ 0 };
	size_t block_processor_full_size{ 65536 };
	size_t block_processor_verification_size{ 0 };
};
}
