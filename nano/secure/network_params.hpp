#pragma once

#include <nano/lib/constants.hpp>
#include <nano/lib/epochs.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>

#include <chrono>
#include <memory>

namespace nano
{
/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (nano::network_type);

	nano::keypair zero_key;
	nano::account nano_beta_account;
	nano::account nano_live_account;
	nano::account nano_test_account;
	std::shared_ptr<nano::block> nano_dev_genesis;
	std::shared_ptr<nano::block> nano_beta_genesis;
	std::shared_ptr<nano::block> nano_live_genesis;
	std::shared_ptr<nano::block> nano_test_genesis;
	std::shared_ptr<nano::block> genesis;
	nano::uint128_t genesis_amount;
	nano::account burn_account;
	nano::epochs epochs;
};

/** Constants which depend on random values (always used as singleton) */
class hardened_constants
{
public:
	static hardened_constants & get ();

	nano::account not_an_account;
	nano::uint128_union random_128;

private:
	hardened_constants ();
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	explicit node_constants (nano::network_constants const &);

	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::minutes unchecked_cleaning_interval;

	// Time between collecting online representative samples
	std::chrono::seconds weight_interval;
	// The maximum time to keep online weight samples: 2 weeks on live or 1 day on beta
	std::chrono::seconds weight_cutoff;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	explicit voting_constants (nano::network_constants const &);

	size_t const max_cache;
	std::chrono::seconds const delay;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	explicit portmapping_constants (nano::network_constants const &);

	// Timeouts are primes so they infrequently happen at the same time
	std::chrono::seconds lease_duration;
	std::chrono::seconds health_check_period;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	explicit bootstrap_constants (nano::network_constants const &);

	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
	std::chrono::milliseconds gap_cache_bootstrap_start_interval;
	uint32_t default_frontiers_age_seconds;
};

/** Caps a vote solicitation round applies, the dev network sends one copy of a winner per election */
struct solicitation_constants final
{
	std::size_t election_vote_requests{ 50 }; // Reps asked for votes per election, a rep that voted for a different hash is exempt
	std::size_t election_block_broadcasts{ 3 }; // Reps without a vote sent the winner block per election
	std::size_t election_fork_broadcasts{ 3 }; // Reps that voted for a different hash sent the winner block per election, on top of the others
	std::size_t channel_messages{ 32 }; // Messages planned per channel and traffic type in one round, what a channel queue takes while below its max threshold
};

nano::work_thresholds work_thresholds_for_network (nano::network_type);

/** Constants whose value depends on the active network */
class network_params
{
public:
	explicit network_params (nano::network_type);

	unsigned kdf_work;
	nano::work_thresholds work;
	nano::network_constants network;
	nano::ledger_constants ledger;
	nano::voting_constants voting;
	nano::node_constants node;
	nano::portmapping_constants portmapping;
	nano::bootstrap_constants bootstrap;
	nano::solicitation_constants solicitation;
};

namespace dev
{
	extern nano::keypair genesis_key;
	extern nano::network_params network_params;
	extern nano::ledger_constants & constants;
	extern std::shared_ptr<nano::block> & genesis;
}
}
