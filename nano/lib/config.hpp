#pragma once

#include <array>
#include <boost/filesystem.hpp>
#include <chrono>
#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <string>

namespace nano
{
class ledger_constants;
class network_params;

/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used in integral comparisons; do not change.
 */
enum class nano_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	nano_test_network = 0,
	rai_test_network = 0,
	// Normal work parameters, secret beta genesis key, beta IP ports
	nano_beta_network = 1,
	rai_beta_network = 1,
	// Normal work parameters, secret live key, live IP ports
	nano_live_network = 2,
	rai_live_network = 2,
};

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (nano::network_params & params_a);
	ledger_constants (nano::nano_networks network_a);
	nano::keypair zero_key;
	nano::keypair test_genesis_key;
	nano::account nano_test_account;
	nano::account nano_beta_account;
	nano::account nano_live_account;
	std::string nano_test_genesis;
	std::string nano_beta_genesis;
	std::string nano_live_genesis;
	nano::account genesis_account;
	std::string genesis_block;
	nano::uint128_t genesis_amount;
	nano::account const & not_an_account ();
	nano::account burn_account;

private:
	nano::account not_an_account_m;
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (nano::network_params & params_a);
	std::chrono::seconds period;
	std::chrono::seconds cutoff;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::seconds peer_interval;
	std::chrono::hours unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;

	/** The maximum amount of samples for a 2 week period on live or 3 days on beta */
	uint64_t max_weight_samples;
	uint64_t weight_period;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (nano::network_params & params_a);
	size_t max_cache;
	std::chrono::milliseconds generator_delay;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (nano::network_params & params_a);
	// Timeouts are primes so they infrequently happen at the same time
	int mapping_timeout;
	int check_timeout;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (nano::network_params & params_a);
	uint64_t lazy_max_pull_blocks;
};

/** Constants whose value depends on the active network */
class network_params
{
public:
	/** Populate values based on the current active network */
	network_params ();

	/** Populate values based on \p network_a */
	network_params (nano::nano_networks network_a);

	/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
	nano::nano_networks current_network;

	std::array<uint8_t, 2> header_magic_number;
	unsigned request_interval_ms;
	uint64_t publish_threshold;
	unsigned kdf_work;
	uint16_t default_node_port;
	uint16_t default_rpc_port;
	ledger_constants ledger;
	voting_constants voting;
	node_constants node;
	portmapping_constants portmapping;
	bootstrap_constants bootstrap;

	/** Returns the network this object contains values for */
	nano::nano_networks network ()
	{
		return current_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network The new active network
	 */
	static void set_active_network (nano::nano_networks network_a)
	{
		active_network = network_a;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "test"
	 */
	static nano::error set_active_network (std::string network_a)
	{
		nano::error err;
		if (network_a == "live")
		{
			active_network = nano::nano_networks::nano_live_network;
		}
		else if (network_a == "beta")
		{
			active_network = nano::nano_networks::nano_beta_network;
		}
		else if (network_a == "test")
		{
			active_network = nano::nano_networks::nano_test_network;
		}
		else
		{
			err = "Invalid network. Valid values are live, beta and test.";
		}
		return err;
	}

	bool is_live_network ()
	{
		return current_network == nano_networks::nano_live_network;
	}
	bool is_beta_network ()
	{
		return current_network == nano_networks::nano_beta_network;
	}
	bool is_test_network ()
	{
		return current_network == nano_networks::nano_test_network;
	}

private:
	/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
	static nano::nano_networks active_network;
};

inline boost::filesystem::path get_config_path (boost::filesystem::path const & data_path)
{
	return data_path / "config.json";
}
}

void force_nano_test_network ();
