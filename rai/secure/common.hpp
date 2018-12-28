#pragma once

#include <rai/lib/blocks.hpp>
#include <rai/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

#include <blake2/blake2.h>

namespace boost
{
template <>
struct hash<rai::uint256_union>
{
	size_t operator() (rai::uint256_union const & value_a) const
	{
		std::hash<rai::uint256_union> hash;
		return hash (value_a);
	}
};
}
namespace rai
{
const uint8_t protocol_version = 0x0f;
const uint8_t protocol_version_min = 0x0d;
const uint8_t node_id_version = 0x0c;

/*
 * Do not bootstrap from nodes older than this version.
 * Also, on the beta network do not process messages from
 * nodes older than this version.
 */
const uint8_t protocol_version_reasonable_min = 0x0d;

/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (rai::raw_key &&);
	rai::public_key pub;
	rai::raw_key prv;
};

/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_0 = 2,
	epoch_1 = 3
};

/**
 * Latest information about an account
 */
class account_info
{
public:
	account_info ();
	account_info (rai::account_info const &) = default;
	account_info (rai::block_hash const &, rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t, uint64_t, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::account_info const &) const;
	bool operator!= (rai::account_info const &) const;
	size_t db_size () const;
	rai::block_hash head;
	rai::block_hash rep_block;
	rai::block_hash open_block;
	rai::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
	rai::epoch epoch;
};

/**
 * Information on an uncollected send
 */
class pending_info
{
public:
	pending_info ();
	pending_info (rai::account const &, rai::amount const &, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_info const &) const;
	rai::account source;
	rai::amount amount;
	rai::epoch epoch;
};
class pending_key
{
public:
	pending_key ();
	pending_key (rai::account const &, rai::block_hash const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_key const &) const;
	rai::account account;
	rai::block_hash hash;
	rai::block_hash key () const;
};
// Internally unchecked_key is equal to pending_key (2x uint256_union)
using unchecked_key = pending_key;

class block_info
{
public:
	block_info ();
	block_info (rai::account const &, rai::amount const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::block_info const &) const;
	rai::account account;
	rai::amount balance;
};
class block_counts
{
public:
	block_counts ();
	size_t sum ();
	size_t send;
	size_t receive;
	size_t open;
	size_t change;
	size_t state_v0;
	size_t state_v1;
};
typedef std::vector<boost::variant<std::shared_ptr<rai::block>, rai::block_hash>>::const_iterator vote_blocks_vec_iter;
class iterate_vote_blocks_as_hash
{
public:
	iterate_vote_blocks_as_hash () = default;
	rai::block_hash operator() (boost::variant<std::shared_ptr<rai::block>, rai::block_hash> const & item) const;
};
class vote
{
public:
	vote () = default;
	vote (rai::vote const &);
	vote (bool &, rai::stream &, rai::block_uniquer * = nullptr);
	vote (bool &, rai::stream &, rai::block_type, rai::block_uniquer * = nullptr);
	vote (rai::account const &, rai::raw_key const &, uint64_t, std::shared_ptr<rai::block>);
	vote (rai::account const &, rai::raw_key const &, uint64_t, std::vector<rai::block_hash>);
	std::string hashes_string () const;
	rai::uint256_union hash () const;
	rai::uint256_union full_hash () const;
	bool operator== (rai::vote const &) const;
	bool operator!= (rai::vote const &) const;
	void serialize (rai::stream &, rai::block_type);
	void serialize (rai::stream &);
	bool deserialize (rai::stream &, rai::block_uniquer * = nullptr);
	bool validate ();
	boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<rai::iterate_vote_blocks_as_hash, rai::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<rai::block>, rai::block_hash>> blocks;
	// Account that's voting
	rai::account account;
	// Signature of sequence + block hashes
	rai::signature signature;
	static const std::string hash_prefix;
};
/**
 * This class serves to find and return unique variants of a vote in order to minimize memory usage
 */
class vote_uniquer
{
public:
	vote_uniquer (rai::block_uniquer &);
	std::shared_ptr<rai::vote> unique (std::shared_ptr<rai::vote>);
	size_t size ();

private:
	rai::block_uniquer & uniquer;
	std::mutex mutex;
	std::unordered_map<rai::uint256_union, std::weak_ptr<rai::vote>> votes;
	static unsigned constexpr cleanup_count = 2;
};
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
class process_return
{
public:
	rai::process_result code;
	rai::account account;
	rai::amount amount;
	rai::account pending_account;
	boost::optional<bool> state_is_send;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};
extern rai::keypair const & zero_key;
extern rai::keypair const & test_genesis_key;
extern rai::account const & rai_test_account;
extern rai::account const & rai_beta_account;
extern rai::account const & rai_live_account;
extern std::string const & rai_test_genesis;
extern std::string const & rai_beta_genesis;
extern std::string const & rai_live_genesis;
extern std::string const & genesis_block;
extern rai::account const & genesis_account;
extern rai::account const & burn_account;
extern rai::uint128_t const & genesis_amount;
// A block hash that compares inequal to any real block hash
extern rai::block_hash const & not_a_block;
// An account number that compares inequal to any real account number
extern rai::block_hash const & not_an_account;
class genesis
{
public:
	explicit genesis ();
	rai::block_hash hash () const;
	std::shared_ptr<rai::block> open;
};
}
