#pragma once

#include <chrono>
#include <nano/lib/config.hpp>
#include <nano/lib/errors.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/ipcconfig.hpp>
#include <nano/node/logging.hpp>
#include <nano/node/stats.hpp>
#include <vector>

namespace nano
{
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
	bool upgrade_json (unsigned, nano::jsonconfig &);
	nano::account random_representative ();
	nano::network_params network_params;
	uint16_t peering_port;
	nano::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<nano::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator;
	nano::amount receive_minimum;
	nano::amount vote_minimum;
	nano::amount online_weight_minimum;
	unsigned online_weight_quorum;
	unsigned password_fanout;
	unsigned io_threads;
	unsigned network_threads;
	unsigned work_threads;
	unsigned signature_checker_threads;
	bool enable_voting;
	unsigned bootstrap_connections;
	unsigned bootstrap_connections_max;
	std::string callback_address;
	uint16_t callback_port;
	std::string callback_target;
	int lmdb_max_dbs;
	bool allow_local_peers;
	nano::stat_config stat_config;
	nano::ipc::ipc_config ipc_config;
	nano::uint256_union epoch_block_link;
	nano::account epoch_block_signer;
	std::chrono::milliseconds block_processor_batch_max_time;
	std::chrono::seconds unchecked_cutoff_time;
	std::chrono::seconds tcp_client_timeout;
	std::chrono::seconds tcp_server_timeout;
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	static int json_version ()
	{
		return 17;
	}
};

class node_flags
{
public:
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_unchecked_cleanup{ false };
	bool disable_unchecked_drop{ true };
	bool fast_bootstrap{ false };
	size_t sideband_batch_size{ 512 };
	size_t block_processor_batch_size{ 0 };
	size_t block_processor_full_size{ 65536 };
	size_t block_processor_verification_size{ 0 };
};
}
