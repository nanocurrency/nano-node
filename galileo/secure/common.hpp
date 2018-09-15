#pragma once

#include <galileo/lib/blocks.hpp>
#include <galileo/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

#include <blake2/blake2.h>

namespace boost
{
template <>
struct hash<galileo::uint256_union>
{
	size_t operator() (galileo::uint256_union const & value_a) const
	{
		std::hash<galileo::uint256_union> hash;
		return hash (value_a);
	}
};
}
namespace galileo
{
const uint8_t protocol_version = 0x0f;
const uint8_t protocol_version_min = 0x07;
const uint8_t node_id_version = 0x0c;

/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (galileo::raw_key &&);
	galileo::public_key pub;
	galileo::raw_key prv;
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
	account_info (galileo::account_info const &) = default;
	account_info (galileo::block_hash const &, galileo::block_hash const &, galileo::block_hash const &, galileo::amount const &, uint64_t, uint64_t, epoch);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	bool operator== (galileo::account_info const &) const;
	bool operator!= (galileo::account_info const &) const;
	size_t db_size () const;
	galileo::block_hash head;
	galileo::block_hash rep_block;
	galileo::block_hash open_block;
	galileo::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
	galileo::epoch epoch;
};

/**
 * Information on an uncollected send
 */
class pending_info
{
public:
	pending_info ();
	pending_info (galileo::account const &, galileo::amount const &, epoch);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	bool operator== (galileo::pending_info const &) const;
	galileo::account source;
	galileo::amount amount;
	galileo::epoch epoch;
};
class pending_key
{
public:
	pending_key ();
	pending_key (galileo::account const &, galileo::block_hash const &);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	bool operator== (galileo::pending_key const &) const;
	galileo::account account;
	galileo::block_hash hash;
};
class block_info
{
public:
	block_info ();
	block_info (galileo::account const &, galileo::amount const &);
	void serialize (galileo::stream &) const;
	bool deserialize (galileo::stream &);
	bool operator== (galileo::block_info const &) const;
	galileo::account account;
	galileo::amount balance;
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
typedef std::vector<boost::variant<std::shared_ptr<galileo::block>, galileo::block_hash>>::const_iterator vote_blocks_vec_iter;
class iterate_vote_blocks_as_hash
{
public:
	iterate_vote_blocks_as_hash () = default;
	galileo::block_hash operator() (boost::variant<std::shared_ptr<galileo::block>, galileo::block_hash> const & item) const;
};
class vote
{
public:
	vote () = default;
	vote (galileo::vote const &);
	vote (bool &, galileo::stream &);
	vote (bool &, galileo::stream &, galileo::block_type);
	vote (galileo::account const &, galileo::raw_key const &, uint64_t, std::shared_ptr<galileo::block>);
	vote (galileo::account const &, galileo::raw_key const &, uint64_t, std::vector<galileo::block_hash>);
	std::string hashes_string () const;
	galileo::uint256_union hash () const;
	bool operator== (galileo::vote const &) const;
	bool operator!= (galileo::vote const &) const;
	void serialize (galileo::stream &, galileo::block_type);
	void serialize (galileo::stream &);
	bool deserialize (galileo::stream &);
	bool validate ();
	boost::transform_iterator<galileo::iterate_vote_blocks_as_hash, galileo::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<galileo::iterate_vote_blocks_as_hash, galileo::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<galileo::block>, galileo::block_hash>> blocks;
	// Account that's voting
	galileo::account account;
	// Signature of sequence + block hashes
	galileo::signature signature;
	static const std::string hash_prefix;
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
	galileo::process_result code;
	galileo::account account;
	galileo::amount amount;
	galileo::account pending_account;
	boost::optional<bool> state_is_send;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};
extern galileo::keypair const & zero_key;
extern galileo::keypair const & test_genesis_key;
extern galileo::account const & galileo_test_account;
extern galileo::account const & galileo_beta_account;
extern galileo::account const & galileo_live_account;
extern std::string const & galileo_test_genesis;
extern std::string const & galileo_beta_genesis;
extern std::string const & galileo_live_genesis;
extern std::string const & genesis_block;
extern galileo::account const & genesis_account;
extern galileo::account const & burn_account;
extern galileo::uint128_t const & genesis_amount;
// A block hash that compares inequal to any real block hash
extern galileo::block_hash const & not_a_block;
// An account number that compares inequal to any real account number
extern galileo::block_hash const & not_an_account;
class genesis
{
public:
	explicit genesis ();
	galileo::block_hash hash () const;
	std::unique_ptr<galileo::open_block> open;
};
}
