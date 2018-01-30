#pragma once

#include <rai/lib/blocks.hpp>
#include <rai/node/utility.hpp>

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
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	rai::public_key pub;
	rai::raw_key prv;
};
class shared_ptr_block_hash
{
public:
	size_t operator() (std::shared_ptr<rai::block> const &) const;
	bool operator() (std::shared_ptr<rai::block> const &, std::shared_ptr<rai::block> const &) const;
};
std::unique_ptr<rai::block> deserialize_block (MDB_val const &);
// Latest information about an account
class account_info
{
public:
	account_info ();
	account_info (MDB_val const &);
	account_info (rai::account_info const &) = default;
	account_info (rai::block_hash const &, rai::block_hash const &, rai::block_hash const &, rai::amount const &, uint64_t, uint64_t);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::account_info const &) const;
	bool operator!= (rai::account_info const &) const;
	rai::mdb_val val () const;
	rai::block_hash head;
	rai::block_hash rep_block;
	rai::block_hash open_block;
	rai::amount balance;
	/** Seconds since posix epoch */
	uint64_t modified;
	uint64_t block_count;
};
class store_entry
{
public:
	store_entry ();
	void clear ();
	store_entry * operator-> ();
	rai::mdb_val first;
	rai::mdb_val second;
};
class store_iterator
{
public:
	store_iterator (MDB_txn *, MDB_dbi);
	store_iterator (std::nullptr_t);
	store_iterator (MDB_txn *, MDB_dbi, MDB_val const &);
	store_iterator (rai::store_iterator &&);
	store_iterator (rai::store_iterator const &) = delete;
	~store_iterator ();
	rai::store_iterator & operator++ ();
	void next_dup ();
	rai::store_iterator & operator= (rai::store_iterator &&);
	rai::store_iterator & operator= (rai::store_iterator const &) = delete;
	rai::store_entry & operator-> ();
	bool operator== (rai::store_iterator const &) const;
	bool operator!= (rai::store_iterator const &) const;
	MDB_cursor * cursor;
	rai::store_entry current;
};
// Information on an uncollected send, source account, amount, target account.
class pending_info
{
public:
	pending_info ();
	pending_info (MDB_val const &);
	pending_info (rai::account const &, rai::amount const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator== (rai::pending_info const &) const;
	rai::mdb_val val () const;
	rai::account source;
	rai::amount amount;
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
class vote_result
{
public:
	rai::vote_code code;
	std::shared_ptr<rai::vote> vote;
};
class block_store
{
public:
	block_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	MDB_dbi block_database (rai::block_type);
	void block_put_raw (MDB_txn *, MDB_dbi, rai::block_hash const &, MDB_val);
	void block_put (MDB_txn *, rai::block_hash const &, rai::block const &, rai::block_hash const & = rai::block_hash (0));
	MDB_val block_get_raw (MDB_txn *, rai::block_hash const &, rai::block_type &);
	rai::block_hash block_successor (MDB_txn *, rai::block_hash const &);
	void block_successor_clear (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> block_get (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> block_random (MDB_txn *);
	std::unique_ptr<rai::block> block_random (MDB_txn *, MDB_dbi);
	void block_del (MDB_txn *, rai::block_hash const &);
	bool block_exists (MDB_txn *, rai::block_hash const &);
	rai::block_counts block_count (MDB_txn *);

	void frontier_put (MDB_txn *, rai::block_hash const &, rai::account const &);
	rai::account frontier_get (MDB_txn *, rai::block_hash const &);
	void frontier_del (MDB_txn *, rai::block_hash const &);
	size_t frontier_count (MDB_txn *);

	void account_put (MDB_txn *, rai::account const &, rai::account_info const &);
	bool account_get (MDB_txn *, rai::account const &, rai::account_info &);
	void account_del (MDB_txn *, rai::account const &);
	bool account_exists (MDB_txn *, rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *, rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *);
	rai::store_iterator latest_end ();

	void pending_put (MDB_txn *, rai::pending_key const &, rai::pending_info const &);
	void pending_del (MDB_txn *, rai::pending_key const &);
	bool pending_get (MDB_txn *, rai::pending_key const &, rai::pending_info &);
	bool pending_exists (MDB_txn *, rai::pending_key const &);
	rai::store_iterator pending_begin (MDB_txn *, rai::pending_key const &);
	rai::store_iterator pending_begin (MDB_txn *);
	rai::store_iterator pending_end ();

	void block_info_put (MDB_txn *, rai::block_hash const &, rai::block_info const &);
	void block_info_del (MDB_txn *, rai::block_hash const &);
	bool block_info_get (MDB_txn *, rai::block_hash const &, rai::block_info &);
	bool block_info_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator block_info_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator block_info_begin (MDB_txn *);
	rai::store_iterator block_info_end ();
	rai::uint128_t block_balance (MDB_txn *, rai::block_hash const &);
	static size_t const block_info_max = 32;

	rai::uint128_t representation_get (MDB_txn *, rai::account const &);
	void representation_put (MDB_txn *, rai::account const &, rai::uint128_t const &);
	void representation_add (MDB_txn *, rai::account const &, rai::uint128_t const &);
	rai::store_iterator representation_begin (MDB_txn *);
	rai::store_iterator representation_end ();

	void unchecked_clear (MDB_txn *);
	void unchecked_put (MDB_txn *, rai::block_hash const &, std::shared_ptr<rai::block> const &);
	std::vector<std::shared_ptr<rai::block>> unchecked_get (MDB_txn *, rai::block_hash const &);
	void unchecked_del (MDB_txn *, rai::block_hash const &, rai::block const &);
	rai::store_iterator unchecked_begin (MDB_txn *);
	rai::store_iterator unchecked_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unchecked_end ();
	size_t unchecked_count (MDB_txn *);
	std::unordered_multimap<rai::block_hash, std::shared_ptr<rai::block>> unchecked_cache;

	void unsynced_put (MDB_txn *, rai::block_hash const &);
	void unsynced_del (MDB_txn *, rai::block_hash const &);
	bool unsynced_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *);
	rai::store_iterator unsynced_end ();

	void checksum_put (MDB_txn *, uint64_t, uint8_t, rai::checksum const &);
	bool checksum_get (MDB_txn *, uint64_t, uint8_t, rai::checksum &);
	void checksum_del (MDB_txn *, uint64_t, uint8_t);

	rai::vote_result vote_validate (MDB_txn *, std::shared_ptr<rai::vote>);
	// Return latest vote for an account from store
	std::shared_ptr<rai::vote> vote_get (MDB_txn *, rai::account const &);
	// Populate vote with the next sequence number
	std::shared_ptr<rai::vote> vote_generate (MDB_txn *, rai::account const &, rai::raw_key const &, std::shared_ptr<rai::block>);
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<rai::vote> vote_max (MDB_txn *, std::shared_ptr<rai::vote>);
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<rai::vote> vote_current (MDB_txn *, rai::account const &);
	void flush (MDB_txn *);
	rai::store_iterator vote_begin (MDB_txn *);
	rai::store_iterator vote_end ();
	std::mutex cache_mutex;
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> vote_cache;

	void version_put (MDB_txn *, int);
	int version_get (MDB_txn *);
	void do_upgrades (MDB_txn *);
	void upgrade_v1_to_v2 (MDB_txn *);
	void upgrade_v2_to_v3 (MDB_txn *);
	void upgrade_v3_to_v4 (MDB_txn *);
	void upgrade_v4_to_v5 (MDB_txn *);
	void upgrade_v5_to_v6 (MDB_txn *);
	void upgrade_v6_to_v7 (MDB_txn *);
	void upgrade_v7_to_v8 (MDB_txn *);
	void upgrade_v8_to_v9 (MDB_txn *);
	void upgrade_v9_to_v10 (MDB_txn *);

	void clear (MDB_dbi);

	rai::mdb_env environment;
	// block_hash -> account                                        // Maps head blocks to owning account
	MDB_dbi frontiers;
	// account -> block_hash, representative, balance, timestamp    // Account to head block, representative, balance, last_change
	MDB_dbi accounts;
	// block_hash -> send_block
	MDB_dbi send_blocks;
	// block_hash -> receive_block
	MDB_dbi receive_blocks;
	// block_hash -> open_block
	MDB_dbi open_blocks;
	// block_hash -> change_block
	MDB_dbi change_blocks;
	// block_hash -> sender, amount, destination                    // Pending blocks to sender account, amount, destination account
	MDB_dbi pending;
	// block_hash -> account, balance                               // Blocks info
	MDB_dbi blocks_info;
	// account -> weight                                            // Representation
	MDB_dbi representation;
	// block_hash -> block                                          // Unchecked bootstrap blocks
	MDB_dbi unchecked;
	// block_hash ->                                                // Blocks that haven't been broadcast
	MDB_dbi unsynced;
	// (uint56_t, uint8_t) -> block_hash                            // Mapping of region to checksum
	MDB_dbi checksum;
	// account -> uint64_t											// Highest vote observed for account
	MDB_dbi vote;
	// uint256_union -> ?											// Meta information about block store
	MDB_dbi meta;
};
enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	overspend, // Malicious attempt to overspend
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist or has already been received
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	not_receive_from_send, // Receive does not have a send source
	account_mismatch, // Account number in open block doesn't match send destination
	opened_burn_account // The impossible happened, someone found the private key associated with the public key '0'.
};
class process_return
{
public:
	rai::process_result code;
	rai::account account;
	rai::amount amount;
	rai::account pending_account;
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
	// Root block of fork
	rai::block_hash id;
	// All votes received by account
	std::unordered_map<rai::account, std::shared_ptr<rai::block>> rep_votes;
};
class ledger
{
public:
	ledger (rai::block_store &, rai::uint128_t const & = 0);
	std::pair<rai::uint128_t, std::shared_ptr<rai::block>> winner (MDB_txn *, rai::votes const & votes_a);
	// Map of weight -> associated block, ordered greatest to least
	std::map<rai::uint128_t, std::shared_ptr<rai::block>, std::greater<rai::uint128_t>> tally (MDB_txn *, rai::votes const &);
	rai::account account (MDB_txn *, rai::block_hash const &);
	rai::uint128_t amount (MDB_txn *, rai::block_hash const &);
	rai::uint128_t balance (MDB_txn *, rai::block_hash const &);
	rai::uint128_t account_balance (MDB_txn *, rai::account const &);
	rai::uint128_t account_pending (MDB_txn *, rai::account const &);
	rai::uint128_t weight (MDB_txn *, rai::account const &);
	std::unique_ptr<rai::block> successor (MDB_txn *, rai::block_hash const &);
	std::unique_ptr<rai::block> forked_block (MDB_txn *, rai::block const &);
	rai::block_hash latest (MDB_txn *, rai::account const &);
	rai::block_hash latest_root (MDB_txn *, rai::account const &);
	rai::block_hash representative (MDB_txn *, rai::block_hash const &);
	rai::block_hash representative_calculated (MDB_txn *, rai::block_hash const &);
	bool block_exists (rai::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (rai::block_hash const &);
	rai::uint128_t supply (MDB_txn *);
	rai::process_return process (MDB_txn *, rai::block const &);
	void rollback (MDB_txn *, rai::block_hash const &);
	void change_latest (MDB_txn *, rai::account const &, rai::block_hash const &, rai::account const &, rai::uint128_union const &, uint64_t);
	void checksum_update (MDB_txn *, rai::block_hash const &);
	rai::checksum checksum (MDB_txn *, rai::account const &, rai::account const &);
	void dump_account_chain (rai::account const &);
	static rai::uint128_t const unit;
	rai::block_store & store;
	rai::uint128_t inactive_supply;
	std::unordered_map<rai::account, rai::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks;
	std::atomic<bool> check_bootstrap_weights;
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
