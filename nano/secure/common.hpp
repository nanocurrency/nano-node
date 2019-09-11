#pragma once

#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/epoch.hpp>
#include <nano/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

namespace boost
{
template <>
struct hash<::nano::uint256_union>
{
	size_t operator() (::nano::uint256_union const & value_a) const
	{
		std::hash<::nano::uint256_union> hash;
		return hash (value_a);
	}
};
template <>
struct hash<::nano::uint512_union>
{
	size_t operator() (::nano::uint512_union const & value_a) const
	{
		std::hash<::nano::uint512_union> hash;
		return hash (value_a);
	}
};
}
namespace nano
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
	keypair (nano::raw_key &&);
	nano::public_key pub;
	nano::raw_key prv;
};

/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (nano::block_hash const &, nano::account const &, nano::block_hash const &, nano::amount const &, uint64_t, uint64_t, epoch);
	bool deserialize (nano::stream &);
	bool operator== (nano::account_info const &) const;
	bool operator!= (nano::account_info const &) const;
	size_t db_size () const;
	nano::epoch epoch () const;
	nano::block_hash head{ 0 };
	nano::account representative{ 0 };
	nano::block_hash open_block{ 0 };
	nano::amount balance{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	nano::epoch epoch_m{ nano::epoch::epoch_0 };
};

/**
 * Information on an uncollected send
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (nano::account const &, nano::amount const &, epoch);
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_info const &) const;
	nano::account source{ 0 };
	nano::amount amount{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
class pending_key final
{
public:
	pending_key () = default;
	pending_key (nano::account const &, nano::block_hash const &);
	bool deserialize (nano::stream &);
	bool operator== (nano::pending_key const &) const;
	nano::block_hash key () const;
	nano::account account{ 0 };
	nano::block_hash hash{ 0 };
};

class endpoint_key final
{
public:
	endpoint_key () = default;

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	const std::array<uint8_t, 16> & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

// Internally unchecked_key is equal to pending_key (2x uint256_union)
using unchecked_key = pending_key;

/**
 * Tag for block signature verification result
 */
enum class signature_verification : uint8_t
{
	unknown = 0,
	invalid = 1,
	valid = 2,
	valid_epoch = 3 // Valid for epoch blocks
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<nano::block>, nano::account const &, uint64_t, nano::signature_verification = nano::signature_verification::unknown);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	std::shared_ptr<nano::block> block;
	nano::account account{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	nano::signature_verification verified{ nano::signature_verification::unknown };
};

class block_info final
{
public:
	block_info () = default;
	block_info (nano::account const &, nano::amount const &);
	nano::account account{ 0 };
	nano::amount balance{ 0 };
};
class block_counts final
{
public:
	size_t sum () const;
	size_t send{ 0 };
	size_t receive{ 0 };
	size_t open{ 0 };
	size_t change{ 0 };
	size_t state_v0{ 0 };
	size_t state_v1{ 0 };
};
using vote_blocks_vec_iter = std::vector<boost::variant<std::shared_ptr<nano::block>, nano::block_hash>>::const_iterator;
class iterate_vote_blocks_as_hash final
{
public:
	iterate_vote_blocks_as_hash () = default;
	nano::block_hash operator() (boost::variant<std::shared_ptr<nano::block>, nano::block_hash> const & item) const;
};
class vote final
{
public:
	vote () = default;
	vote (nano::vote const &);
	vote (bool &, nano::stream &, nano::block_uniquer * = nullptr);
	vote (bool &, nano::stream &, nano::block_type, nano::block_uniquer * = nullptr);
	vote (nano::account const &, nano::raw_key const &, uint64_t, std::shared_ptr<nano::block>);
	vote (nano::account const &, nano::raw_key const &, uint64_t, std::vector<nano::block_hash> const &);
	std::string hashes_string () const;
	nano::uint256_union hash () const;
	nano::uint256_union full_hash () const;
	bool operator== (nano::vote const &) const;
	bool operator!= (nano::vote const &) const;
	void serialize (nano::stream &, nano::block_type) const;
	void serialize (nano::stream &) const;
	void serialize_json (boost::property_tree::ptree & tree) const;
	bool deserialize (nano::stream &, nano::block_uniquer * = nullptr);
	bool validate () const;
	boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<nano::iterate_vote_blocks_as_hash, nano::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<nano::block>, nano::block_hash>> blocks;
	// Account that's voting
	nano::account account;
	// Signature of sequence + block hashes
	nano::signature signature;
	static const std::string hash_prefix;
};
/**
 * This class serves to find and return unique variants of a vote in order to minimize memory usage
 */
class vote_uniquer final
{
public:
	using value_type = std::pair<const nano::uint256_union, std::weak_ptr<nano::vote>>;

	vote_uniquer (nano::block_uniquer &);
	std::shared_ptr<nano::vote> unique (std::shared_ptr<nano::vote>);
	size_t size ();

private:
	nano::block_uniquer & uniquer;
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> votes;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_uniquer & vote_uniquer, const std::string & name);

enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest sequence number, it's a replay
	vote // Vote has the highest sequence number
};

enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position // This block cannot follow the previous block
};
class process_return final
{
public:
	nano::process_result code;
	nano::account account;
	nano::amount amount;
	nano::account pending_account;
	boost::optional<bool> state_is_send;
	nano::signature_verification verified;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};

class genesis final
{
public:
	genesis ();
	nano::block_hash hash () const;
	std::shared_ptr<nano::block> open;
};

class network_params;

/** Protocol versions whose value may depend on the active network */
class protocol_constants
{
public:
	protocol_constants (nano::nano_networks network_a);

	/** Current protocol version */
	uint8_t protocol_version = 0x11;

	/** Minimum accepted protocol version */
	uint8_t protocol_version_min = 0x0d;

	/** Do not bootstrap from nodes older than this version. */
	uint8_t protocol_version_bootstrap_min = 0x0d;

	/** Do not lazy bootstrap from nodes older than this version. */
	uint8_t protocol_version_bootstrap_lazy_min = 0x10;

	/** Do not start TCP realtime network connections to nodes older than this version */
	uint8_t tcp_realtime_protocol_version_min = 0x11;
};

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (nano::network_constants & network_constants);
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
	nano::account burn_account;
	nano::epochs epochs;
};

/** Constants which depend on random values (this class should never be used globally due to CryptoPP globals potentially not being initialized) */
class random_constants
{
public:
	random_constants ();
	nano::account not_an_account;
	nano::uint128_union random_128;
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (nano::network_constants & network_constants);
	std::chrono::seconds period;
	std::chrono::milliseconds half_period;
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds idle_timeout;
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
	voting_constants (nano::network_constants & network_constants);
	size_t max_cache;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (nano::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	int mapping_timeout;
	int check_timeout;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (nano::network_constants & network_constants);
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

	std::array<uint8_t, 2> header_magic_number;
	unsigned kdf_work;
	network_constants network;
	protocol_constants protocol;
	ledger_constants ledger;
	random_constants random;
	voting_constants voting;
	node_constants node;
	portmapping_constants portmapping;
	bootstrap_constants bootstrap;
};
}
