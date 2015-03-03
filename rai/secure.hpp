#pragma once 

#include <rai/config.hpp>
#include <rai/utility.hpp>

#include <boost/property_tree/ptree.hpp>

#include <unordered_map>

namespace std
{
template <>
struct hash <rai::uint256_union>
{
	size_t operator () (rai::uint256_union const & data_a) const
	{
		return *reinterpret_cast <size_t const *> (data_a.bytes.data ());
	}
};
template <>
struct hash <rai::uint256_t>
{
	size_t operator () (rai::uint256_t const & number_a) const
	{
		return number_a.convert_to <size_t> ();
	}
};
}
namespace boost
{
template <>
struct hash <rai::uint256_union>
{
	size_t operator () (rai::uint256_union const & value_a) const
	{
		std::hash <rai::uint256_union> hash;
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
	rai::private_key prv;
};
class block_visitor;
enum class block_type : uint8_t
{
	invalid,
	not_a_block,
	send,
	receive,
	open,
	change
};
class block
{
public:
	// Return a digest of the hashables in this block.
	rai::block_hash hash () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block in account's chain, zero for open block
	virtual rai::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual rai::block_hash source () const = 0;
	// Previous block or account number for open blocks
	virtual rai::block_hash root () const = 0;
	virtual void serialize (rai::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void visit (rai::block_visitor &) const = 0;
	virtual bool operator == (rai::block const &) const = 0;
	virtual std::unique_ptr <rai::block> clone () const = 0;
	virtual rai::block_type type () const = 0;
	// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
	static uint64_t const publish_test_threshold = 0xff00000000000000;
	static uint64_t const publish_full_threshold = 0xfffffe0000000000;
	static uint64_t const publish_threshold = rai::rai_network == rai::rai_networks::rai_test_network ? publish_test_threshold : publish_full_threshold;
};
class unique_ptr_block_hash
{
public:
	size_t operator () (std::unique_ptr <rai::block> const &) const;
	bool operator () (std::unique_ptr <rai::block> const &, std::unique_ptr <rai::block> const &) const;
};
std::unique_ptr <rai::block> deserialize_block (MDB_val const &);
std::unique_ptr <rai::block> deserialize_block (rai::stream &);
std::unique_ptr <rai::block> deserialize_block (rai::stream &, rai::block_type);
std::unique_ptr <rai::block> deserialize_block_json (boost::property_tree::ptree const &);
void serialize_block (rai::stream &, rai::block const &);
void work_generate (rai::block &);
uint64_t work_generate (rai::block_hash const &);
bool work_validate (rai::block &);
bool work_validate (rai::block_hash const &, uint64_t);
class send_hashables
{
public:
	send_hashables (rai::account const &, rai::block_hash const &, rai::amount const &);
	send_hashables (bool &, rai::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::account destination;
	rai::block_hash previous;
	rai::amount balance;
};
class send_block : public rai::block
{
public:
	send_block (rai::account const &, rai::block_hash const &, rai::amount const &, rai::private_key const &, rai::public_key const &, uint64_t);
	send_block (bool &, rai::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	std::unique_ptr <rai::block> clone () const override;
	rai::block_type type () const override;
	bool operator == (rai::block const &) const override;
	bool operator == (rai::send_block const &) const;
	static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::amount) + sizeof (rai::signature) + sizeof (uint64_t);
	send_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class receive_hashables
{
public:
	receive_hashables (rai::block_hash const &, rai::block_hash const &);
	receive_hashables (bool &, rai::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::block_hash previous;
	rai::block_hash source;
};
class receive_block : public rai::block
{
public:
	receive_block (rai::block_hash const &, rai::block_hash const &, rai::private_key const &, rai::public_key const &, uint64_t);
	receive_block (bool &, rai::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	std::unique_ptr <rai::block> clone () const override;
	rai::block_type type () const override;
	bool operator == (rai::block const &) const override;
	bool operator == (rai::receive_block const &) const;
	static size_t constexpr size = sizeof (rai::block_hash) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
	receive_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class open_hashables
{
public:
	open_hashables (rai::account const &, rai::account const &, rai::block_hash const &);
	open_hashables (bool &, rai::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::account account;
	rai::account representative;
	rai::block_hash source;
};
class open_block : public rai::block
{
public:
	open_block (rai::account const &, rai::account const &, rai::block_hash const &, rai::private_key const &, rai::public_key const &, uint64_t);
	open_block (rai::account const &, rai::account const &, rai::block_hash const &, std::nullptr_t);
	open_block (bool &, rai::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	std::unique_ptr <rai::block> clone () const override;
	rai::block_type type () const override;
	bool operator == (rai::block const &) const override;
	bool operator == (rai::open_block const &) const;
	static size_t constexpr size = sizeof (rai::account) + sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
	rai::open_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class change_hashables
{
public:
	change_hashables (rai::account const &, rai::block_hash const &);
	change_hashables (bool &, rai::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	rai::account representative;
	rai::block_hash previous;
};
class change_block : public rai::block
{
public:
	change_block (rai::account const &, rai::block_hash const &, rai::private_key const &, rai::public_key const &, uint64_t);
	change_block (bool &, rai::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	using rai::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	rai::block_hash previous () const override;
	rai::block_hash source () const override;
	rai::block_hash root () const override;
	void serialize (rai::stream &) const override;
	void serialize_json (std::string &) const override;
	bool deserialize (rai::stream &);
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (rai::block_visitor &) const override;
	std::unique_ptr <rai::block> clone () const override;
	rai::block_type type () const override;
	bool operator == (rai::block const &) const override;
	bool operator == (rai::change_block const &) const;
	static size_t constexpr size = sizeof (rai::account) + sizeof (rai::block_hash) + sizeof (rai::signature) + sizeof (uint64_t);
	rai::change_hashables hashables;
	rai::signature signature;
	uint64_t work;
};
class block_visitor
{
public:
	virtual void send_block (rai::send_block const &) = 0;
	virtual void receive_block (rai::receive_block const &) = 0;
	virtual void open_block (rai::open_block const &) = 0;
	virtual void change_block (rai::change_block const &) = 0;
};
struct block_store_temp_t
{
};
// Latest information about an account, latest block, current representative, balance
class frontier
{
public:
	frontier ();
	frontier (MDB_val const &);
	frontier (rai::block_hash const &, rai::account const &, rai::amount const &, uint64_t);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator == (rai::frontier const &) const;
	bool operator != (rai::frontier const &) const;
	rai::mdb_val val () const;
	rai::block_hash hash;
	rai::account representative;
	rai::amount balance;
	uint64_t time;
};
class store_entry
{
public:
	store_entry ();
	void clear ();
	store_entry * operator -> ();
	MDB_val first;
	MDB_val second;
};
class store_iterator
{
public:
	store_iterator (MDB_txn *, MDB_dbi);
	store_iterator (std::nullptr_t);
	store_iterator (MDB_txn *, MDB_dbi, MDB_val const &);
	store_iterator (rai::store_iterator const &) = default;
	store_iterator (rai::store_iterator &&) = default;
	~store_iterator ();
	rai::store_iterator & operator ++ ();
	rai::store_iterator & operator = (rai::store_iterator &&) = default;
	rai::store_entry & operator -> ();
	bool operator == (rai::store_iterator const &) const;
	bool operator != (rai::store_iterator const &) const;
	MDB_cursor * cursor;
	rai::store_entry current;
};
// Information on an uncollected send, source account, amount, target account.
class receivable
{
public:
	receivable ();
	receivable (MDB_val const &);
	receivable (rai::account const &, rai::amount const &, rai::account const &);
	void serialize (rai::stream &) const;
	bool deserialize (rai::stream &);
	bool operator == (rai::receivable const &) const;
	rai::mdb_val val () const;
	rai::account source;
	rai::amount amount;
	rai::account destination;
};
class block_store
{
public:
	block_store (bool &, boost::filesystem::path const &);
	uint64_t now ();
	
	void block_put_raw (MDB_txn *, rai::block_hash const &, MDB_val);
	void block_put (MDB_txn *, rai::block_hash const &, rai::block const &);
	MDB_val block_get_raw (MDB_txn *, rai::block_hash const &);
	rai::block_hash block_successor (MDB_txn *, rai::block_hash const &);
	std::unique_ptr <rai::block> block_get (MDB_txn *, rai::block_hash const &);
	void block_del (MDB_txn *, rai::block_hash const &);
	bool block_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator blocks_begin (MDB_txn *, rai::uint256_union const &);
	rai::store_iterator blocks_begin (MDB_txn *);
	rai::store_iterator blocks_end ();
	
	void latest_put (MDB_txn *, rai::account const &, rai::frontier const &);
	bool latest_get (MDB_txn *, rai::account const &, rai::frontier &);
	bool latest_get (rai::account const &, rai::frontier &);
	void latest_del (rai::account const &);
	bool latest_exists (rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *, rai::account const &);
	rai::store_iterator latest_begin (MDB_txn *);
	rai::store_iterator latest_end ();
	
	void pending_put (MDB_txn *, rai::block_hash const &, rai::receivable const &);
	void pending_del (MDB_txn *, rai::block_hash const &);
	bool pending_get (MDB_txn *, rai::block_hash const &, rai::receivable &);
	bool pending_get (rai::block_hash const &, rai::receivable &);
	bool pending_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator pending_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator pending_begin (MDB_txn *);
	rai::store_iterator pending_end ();
	
	rai::uint128_t representation_get (rai::account const &);
	void representation_put (MDB_txn *, rai::account const &, rai::uint128_t const &);
	
	void unchecked_put (rai::block_hash const &, rai::block const &);
	std::unique_ptr <rai::block> unchecked_get (rai::block_hash const &);
	void unchecked_del (rai::block_hash const &);
	rai::store_iterator unchecked_begin (MDB_txn *);
	rai::store_iterator unchecked_end ();
	
	void unsynced_put (MDB_txn *, rai::block_hash const &);
	void unsynced_del (MDB_txn *, rai::block_hash const &);
	bool unsynced_exists (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *, rai::block_hash const &);
	rai::store_iterator unsynced_begin (MDB_txn *);
	rai::store_iterator unsynced_end ();

	void stack_open ();
	void stack_push (uint64_t, rai::block_hash const &);
	rai::block_hash stack_pop (uint64_t);
	
	void checksum_put (MDB_txn *, uint64_t, uint8_t, rai::checksum const &);
	bool checksum_get (MDB_txn *, uint64_t, uint8_t, rai::checksum &);
	void checksum_del (uint64_t, uint8_t);
	
	void clear (MDB_dbi);
	
	rai::mdb_env environment;
private:
	// account -> block_hash, representative, balance, timestamp    // Account to frontier block, representative, balance, last_change
	MDB_dbi accounts;
	// block_hash -> block                                          // Mapping block hash to contents
	MDB_dbi blocks;
	// block_hash -> sender, amount, destination                    // Pending blocks to sender account, amount, destination account
	MDB_dbi pending;
	// account -> weight                                            // Representation
	MDB_dbi representation;
	// block_hash -> block                                          // Unchecked bootstrap blocks
	MDB_dbi unchecked;
	// block_hash ->                                                // Blocks that haven't been broadcast
	MDB_dbi unsynced;
	// uint64_t -> block_hash                                       // Block dependency stack while bootstrapping
	MDB_dbi stack;
	// (uint56_t, uint8_t) -> block_hash                            // Mapping of region to checksum
	MDB_dbi checksum;
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
	account_mismatch // Account number in open block doesn't match send destination
};
class vote
{
public:
	vote (bool &, rai::stream &, rai::block_type);
	vote (rai::account const &, rai::private_key const &, uint64_t, std::unique_ptr <rai::block>);
	rai::uint256_union hash () const;
	// Vote round sequence number
	uint64_t sequence;
	std::unique_ptr <rai::block> block;
	// Account that's voting
	rai::account account;
	// Signature of sequence + block hash
	rai::signature signature;
};
class votes
{
public:
	votes (rai::block_hash const &);
	bool vote (rai::vote const &);
	// Our vote round sequence number
	uint64_t sequence;
	// Root block of fork
	rai::block_hash id;
	// All votes received by account
	std::unordered_map <rai::account, std::pair <uint64_t, std::unique_ptr <rai::block>>> rep_votes;
};
class kdf
{
public:
	kdf (size_t);
	rai::uint256_union generate (std::string const &, rai::uint256_union const &);
	size_t const entries;
	std::unique_ptr <uint64_t []> data;
};
class ledger
{
public:
	ledger (rai::block_store &);
	std::pair <rai::uint128_t, std::unique_ptr <rai::block>> winner (rai::votes const & votes_a);
	std::map <rai::uint128_t, std::unique_ptr <rai::block>, std::greater <rai::uint128_t>> tally (rai::votes const &);
	rai::account account (rai::block_hash const &);
	rai::uint128_t amount (rai::block_hash const &);
	rai::uint128_t balance (rai::block_hash const &);
	rai::uint128_t account_balance (MDB_txn *, rai::account const &);
	rai::uint128_t account_balance (rai::account const &);
	rai::uint128_t weight (rai::account const &);
	std::unique_ptr <rai::block> successor (rai::block_hash const &);
	rai::block_hash latest (rai::account const &);
	rai::block_hash latest_root (rai::account const &);
	rai::account representative (rai::block_hash const &);
	rai::account representative_calculated (rai::block_hash const &);
	rai::account representative_cached (rai::block_hash const &);
	rai::uint128_t supply ();
	rai::process_result process (rai::block const &);
	void rollback (rai::block_hash const &);
	void change_latest (rai::account const &, rai::block_hash const &, rai::account const &, rai::uint128_union const &);
	void move_representation (rai::account const &, rai::account const &, rai::uint128_t const &);
	void checksum_update (rai::block_hash const &);
	rai::checksum checksum (MDB_txn *, rai::account const &, rai::account const &);
	void dump_account_chain (rai::account const &);
	rai::block_store & store;
	std::function <void (rai::send_block const &, rai::account const &, rai::amount const &)> send_observer;
	std::function <void (rai::receive_block const &, rai::account const &, rai::amount const &)> receive_observer;
	std::function <void (rai::open_block const &, rai::account const &, rai::amount const &, rai::account const &)> open_observer;
	std::function <void (rai::change_block const &, rai::account const &, rai::account const &)> change_observer;
};
extern rai::keypair const test_genesis_key;
extern rai::account const rai_test_account;
extern rai::account const rai_beta_account;
extern rai::account const rai_live_account;
extern rai::account const genesis_account;
extern rai::uint128_t const genesis_amount;
class genesis
{
public:
	explicit genesis ();
	void initialize (MDB_txn *, rai::block_store &) const;
	rai::block_hash hash () const;
	rai::open_block open;
};
}