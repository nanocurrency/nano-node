#pragma once

#include <rai/lib/blocks.hpp>
#include <rai/secure/utility.hpp>

#include <boost/property_tree/ptree.hpp>

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
const uint8_t protocol_version = 0x0c;
const uint8_t protocol_version_min = 0x07;
const uint8_t node_id_version = 0x0c;

class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public rai::block_visitor
{
public:
	balance_visitor (MDB_txn *, rai::block_store &);
	virtual ~balance_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current_balance;
	rai::block_hash current_amount;
	rai::uint128_t balance;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public rai::block_visitor
{
public:
	amount_visitor (MDB_txn *, rai::block_store &);
	virtual ~amount_visitor () = default;
	void compute (rai::block_hash const &);
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
	void from_send (rai::block_hash const &);
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current_amount;
	rai::block_hash current_balance;
	rai::uint128_t amount;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public rai::block_visitor
{
public:
	representative_visitor (MDB_txn * transaction_a, rai::block_store & store_a);
	virtual ~representative_visitor () = default;
	void compute (rai::block_hash const & hash_a);
	void send_block (rai::send_block const & block_a) override;
	void receive_block (rai::receive_block const & block_a) override;
	void open_block (rai::open_block const & block_a) override;
	void change_block (rai::change_block const & block_a) override;
	void state_block (rai::state_block const & block_a) override;
	MDB_txn * transaction;
	rai::block_store & store;
	rai::block_hash current;
	rai::block_hash result;
};

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

std::unique_ptr<rai::block> deserialize_block (MDB_val const &);

/**
 * Latest information about an account
 */
class account_info
{
public:
	account_info ();
	account_info (rai::mdb_val const &);
	account_info (rai::account_info const &) = default;
	account_info (rai::block_hash const &, rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t, uint64_t, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::account_info const &) const;
	bool operator!= (rai::account_info const &) const;
	rai::mdb_val val () const;
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
	pending_info (rai::mdb_val const &);
	pending_info (rai::account const &, rai::amount const &, epoch);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_info const &) const;
	rai::mdb_val val () const;
	rai::account source;
	rai::amount amount;
	rai::epoch epoch;
};
class pending_key
{
public:
	pending_key (rai::account const &, rai::block_hash const &);
	pending_key (MDB_val const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_key const &) const;
	rai::mdb_val val () const;
	rai::account account;
	rai::block_hash hash;
};
class block_info
{
public:
	block_info ();
	block_info (MDB_val const &);
	block_info (rai::account const &, rai::amount const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::block_info const &) const;
	rai::mdb_val val () const;
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
class vote
{
public:
	vote () = default;
	vote (rai::vote const &);
	vote (bool &, rai::stream &);
	vote (bool &, rai::stream &, rai::block_type);
	vote (rai::account const &, rai::raw_key const &, uint64_t, std::shared_ptr<rai::block>);
	vote (MDB_val const &);
	rai::uint256_union hash () const;
	bool operator== (rai::vote const &) const;
	bool operator!= (rai::vote const &) const;
	void serialize (rai::stream &, rai::block_type);
	void serialize (rai::stream &);
	bool deserialize (rai::stream &);
	bool validate ();
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	std::shared_ptr<rai::block> block;
	// Account that's voting
	rai::account account;
	// Signature of sequence + block hash
	rai::signature signature;
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
class votes
{
public:
	votes (std::shared_ptr<rai::block>);
	rai::tally_result vote (std::shared_ptr<rai::vote>);
	bool uncontested ();
	// Root block of fork
	rai::block_hash id;
	// All votes received by account
	std::unordered_map<rai::account, std::shared_ptr<rai::block>> rep_votes;
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
	void initialize (MDB_txn *, rai::block_store &) const;
	rai::block_hash hash () const;
	std::unique_ptr<rai::open_block> open;
};
}
