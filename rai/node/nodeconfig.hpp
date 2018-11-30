#pragma once

#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <rai/lib/numbers.hpp>
#include <rai/node/logging.hpp>
#include <rai/node/stats.hpp>
#include <vector>

namespace rai
{
/**
 * Node configuration
 */
class node_config
{
public:
	node_config ();
	node_config (uint16_t, rai::logging const &);
	void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	bool upgrade_json (unsigned, boost::property_tree::ptree &);
	rai::account random_representative ();
	uint16_t peering_port;
	rai::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<rai::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator;
	rai::amount receive_minimum;
	rai::amount vote_minimum;
	rai::amount online_weight_minimum;
	unsigned online_weight_quorum;
	unsigned password_fanout;
	unsigned io_threads;
	unsigned network_threads;
	unsigned work_threads;
	bool enable_voting;
	unsigned bootstrap_connections;
	unsigned bootstrap_connections_max;
	std::string callback_address;
	uint16_t callback_port;
	std::string callback_target;
	int lmdb_max_dbs;
	bool allow_local_peers;
	rai::stat_config stat_config;
	rai::uint256_union epoch_block_link;
	rai::account epoch_block_signer;
	std::chrono::milliseconds block_processor_batch_max_time;
	bool disable_lazy_bootstrap;
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	static constexpr int json_version = 16;
};
}
