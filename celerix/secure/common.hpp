#pragma once

#include <celerix/crypto/blake2/blake2.h>
#include <celerix/lib/blockbuilders.hpp>
#include <celerix/lib/common.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/constants.hpp>
#include <celerix/lib/epochs.hpp>
#include <celerix/lib/fwd.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/object_stream.hpp>
#include <celerix/lib/timer.hpp>
#include <celerix/lib/utility.hpp>

#include <array>
#include <unordered_map>

namespace celerix
{
/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (celerix::raw_key &&);
	celerix::public_key pub;
	celerix::raw_key prv;
};

class endpoint_key final
{
public:
	endpoint_key () = default;
	endpoint_key (celerix::endpoint const &);

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	std::array<uint8_t, 16> const & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

	celerix::endpoint endpoint () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	explicit unchecked_key (celerix::hash_or_account const & dependency);
	unchecked_key (celerix::hash_or_account const &, celerix::block_hash const &);
	unchecked_key (celerix::uint512_union const &);
	bool deserialize (celerix::stream &);
	bool operator== (celerix::unchecked_key const &) const;
	bool operator< (celerix::unchecked_key const &) const;
	celerix::block_hash const & key () const;
	celerix::block_hash previous{ 0 };
	celerix::block_hash hash{ 0 };
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<celerix::block> const &);
	void serialize (celerix::stream &) const;
	bool deserialize (celerix::stream &);
	celerix::seconds_t modified () const;
	std::shared_ptr<celerix::block> block;

private:
	/** Seconds since posix epoch */
	uint64_t modified_m{ 0 };
};

class block_info final
{
public:
	block_info () = default;
	block_info (celerix::account const &, celerix::amount const &);
	celerix::account account{};
	celerix::amount balance{ 0 };
};

class confirmation_height_info final
{
public:
	confirmation_height_info () = default;
	confirmation_height_info (uint64_t, celerix::block_hash const &);

	void serialize (celerix::stream &) const;
	bool deserialize (celerix::stream &);

	/** height of the cemented frontier */
	uint64_t height{};

	/** hash of the highest cemented block, the cemented/confirmed frontier */
	celerix::block_hash frontier{};
};

namespace confirmation_height
{
	/** When the uncemented count (block count - cemented count) is less than this use the unbounded processor */
	uint64_t const unbounded_cutoff{ 16384 };
}

enum class block_status
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	gap_epoch_open_pending, // Block marked as pending blocks required for epoch open block are unknown
	opened_burn_account, // Block attempts to open the burn account
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
	insufficient_work // Insufficient work for this block, even though it passed the minimal validation
};

std::string_view to_string (block_status);
celerix::stat::detail to_stat_detail (block_status);

enum class tally_result
{
	vote,
	changed,
	confirm
};

class network_params;

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (celerix::work_thresholds &, celerix::networks);
	celerix::work_thresholds & work;
	celerix::keypair zero_key;
	celerix::account celerix_beta_account;
	celerix::account celerix_live_account;
	celerix::account celerix_test_account;
	std::shared_ptr<celerix::block> celerix_dev_genesis;
	std::shared_ptr<celerix::block> celerix_beta_genesis;
	std::shared_ptr<celerix::block> celerix_live_genesis;
	std::shared_ptr<celerix::block> celerix_test_genesis;
	std::shared_ptr<celerix::block> genesis;
	celerix::uint128_t genesis_amount;
	celerix::account burn_account;
	celerix::epochs epochs;
};

namespace dev
{
	extern celerix::keypair genesis_key;
	extern celerix::network_params network_params;
	extern celerix::ledger_constants & constants;
	extern std::shared_ptr<celerix::block> & genesis;
}

/** Constants which depend on random values (always used as singleton) */
class hardened_constants
{
public:
	static hardened_constants & get ();

	celerix::account not_an_account;
	celerix::uint128_union random_128;

private:
	hardened_constants ();
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (celerix::network_constants & network_constants);
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::minutes unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;

	/** Time between collecting online representative samples */
	std::chrono::seconds weight_interval;
	/** The maximum time to keep online weight samples: 2 weeks on live or 1 day on beta */
	std::chrono::seconds weight_cutoff;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (celerix::network_constants & network_constants);
	size_t const max_cache;
	std::chrono::seconds const delay;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (celerix::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	std::chrono::seconds lease_duration;
	std::chrono::seconds health_check_period;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (celerix::network_constants & network_constants);
	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
	std::chrono::milliseconds gap_cache_bootstrap_start_interval;
	uint32_t default_frontiers_age_seconds;
};

celerix::work_thresholds const & work_thresholds_for_network (celerix::networks);

/** Constants whose value depends on the active network */
class network_params
{
public:
	explicit network_params (celerix::networks);

	unsigned kdf_work;
	celerix::work_thresholds work;
	celerix::network_constants network;
	celerix::ledger_constants ledger;
	celerix::voting_constants voting;
	celerix::node_constants node;
	celerix::portmapping_constants portmapping;
	celerix::bootstrap_constants bootstrap;
};

celerix::wallet_id random_wallet_id ();
}
