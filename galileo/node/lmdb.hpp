#pragma once

#include <boost/filesystem.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <galileo/lib/numbers.hpp>
#include <galileo/secure/blockstore.hpp>
#include <galileo/secure/common.hpp>

namespace galileo
{
class mdb_env;
class mdb_txn : public transaction_impl
{
public:
	mdb_txn (galileo::mdb_env const &, bool = false);
	mdb_txn (galileo::mdb_txn const &) = delete;
	mdb_txn (galileo::mdb_txn &&) = default;
	~mdb_txn ();
	galileo::mdb_txn & operator= (galileo::mdb_txn const &) = delete;
	galileo::mdb_txn & operator= (galileo::mdb_txn &&) = default;
	operator MDB_txn * () const;
	MDB_txn * handle;
};
/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128);
	~mdb_env ();
	operator MDB_env * () const;
	galileo::transaction tx_begin (bool = false) const;
	MDB_txn * tx (galileo::transaction const &) const;
	MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
	enum class no_value
	{
		dummy
	};
	mdb_val (galileo::epoch = galileo::epoch::unspecified);
	mdb_val (galileo::account_info const &);
	mdb_val (galileo::block_info const &);
	mdb_val (MDB_val const &, galileo::epoch = galileo::epoch::unspecified);
	mdb_val (galileo::pending_info const &);
	mdb_val (galileo::pending_key const &);
	mdb_val (size_t, void *);
	mdb_val (galileo::uint128_union const &);
	mdb_val (galileo::uint256_union const &);
	mdb_val (std::shared_ptr<galileo::block> const &);
	mdb_val (std::shared_ptr<galileo::vote> const &);
	void * data () const;
	size_t size () const;
	explicit operator galileo::account_info () const;
	explicit operator galileo::block_info () const;
	explicit operator galileo::pending_info () const;
	explicit operator galileo::pending_key () const;
	explicit operator galileo::uint128_union () const;
	explicit operator galileo::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator no_value () const;
	explicit operator std::shared_ptr<galileo::block> () const;
	explicit operator std::shared_ptr<galileo::send_block> () const;
	explicit operator std::shared_ptr<galileo::receive_block> () const;
	explicit operator std::shared_ptr<galileo::open_block> () const;
	explicit operator std::shared_ptr<galileo::change_block> () const;
	explicit operator std::shared_ptr<galileo::state_block> () const;
	explicit operator std::shared_ptr<galileo::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	galileo::epoch epoch;
};
class block_store;
/**
 * Determine the balance as of this block
 */
class balance_visitor : public galileo::block_visitor
{
public:
	balance_visitor (galileo::transaction const &, galileo::block_store &);
	virtual ~balance_visitor () = default;
	void compute (galileo::block_hash const &);
	void send_block (galileo::send_block const &) override;
	void receive_block (galileo::receive_block const &) override;
	void open_block (galileo::open_block const &) override;
	void change_block (galileo::change_block const &) override;
	void state_block (galileo::state_block const &) override;
	galileo::transaction const & transaction;
	galileo::block_store & store;
	galileo::block_hash current_balance;
	galileo::block_hash current_amount;
	galileo::uint128_t balance;
};

/**
 * Determine the amount delta resultant from this block
 */
class amount_visitor : public galileo::block_visitor
{
public:
	amount_visitor (galileo::transaction const &, galileo::block_store &);
	virtual ~amount_visitor () = default;
	void compute (galileo::block_hash const &);
	void send_block (galileo::send_block const &) override;
	void receive_block (galileo::receive_block const &) override;
	void open_block (galileo::open_block const &) override;
	void change_block (galileo::change_block const &) override;
	void state_block (galileo::state_block const &) override;
	void from_send (galileo::block_hash const &);
	galileo::transaction const & transaction;
	galileo::block_store & store;
	galileo::block_hash current_amount;
	galileo::block_hash current_balance;
	galileo::uint128_t amount;
};

/**
 * Determine the representative for this block
 */
class representative_visitor : public galileo::block_visitor
{
public:
	representative_visitor (galileo::transaction const & transaction_a, galileo::block_store & store_a);
	virtual ~representative_visitor () = default;
	void compute (galileo::block_hash const & hash_a);
	void send_block (galileo::send_block const & block_a) override;
	void receive_block (galileo::receive_block const & block_a) override;
	void open_block (galileo::open_block const & block_a) override;
	void change_block (galileo::change_block const & block_a) override;
	void state_block (galileo::state_block const & block_a) override;
	galileo::transaction const & transaction;
	galileo::block_store & store;
	galileo::block_hash current;
	galileo::block_hash result;
};
template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (galileo::transaction const & transaction_a, MDB_dbi db_a, galileo::epoch = galileo::epoch::unspecified);
	mdb_iterator (std::nullptr_t, galileo::epoch = galileo::epoch::unspecified);
	mdb_iterator (galileo::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, galileo::epoch = galileo::epoch::unspecified);
	mdb_iterator (galileo::mdb_iterator<T, U> && other_a);
	mdb_iterator (galileo::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	galileo::store_iterator_impl<T, U> & operator++ () override;
	std::pair<galileo::mdb_val, galileo::mdb_val> * operator-> ();
	bool operator== (galileo::store_iterator_impl<T, U> const & other_a) const override;
	void next_dup () override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	galileo::mdb_iterator<T, U> & operator= (galileo::mdb_iterator<T, U> && other_a);
	galileo::store_iterator_impl<T, U> & operator= (galileo::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<galileo::mdb_val, galileo::mdb_val> current;

private:
	MDB_txn * tx (galileo::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (galileo::transaction const &, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (galileo::transaction const &, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (galileo::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (galileo::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	galileo::store_iterator_impl<T, U> & operator++ () override;
	std::pair<galileo::mdb_val, galileo::mdb_val> * operator-> ();
	bool operator== (galileo::store_iterator_impl<T, U> const &) const override;
	void next_dup () override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	galileo::mdb_merge_iterator<T, U> & operator= (galileo::mdb_merge_iterator<T, U> &&) = default;
	galileo::mdb_merge_iterator<T, U> & operator= (galileo::mdb_merge_iterator<T, U> const &) = delete;

private:
	galileo::mdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<galileo::mdb_iterator<T, U>> impl1;
	std::unique_ptr<galileo::mdb_iterator<T, U>> impl2;
};

/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store
{
	friend class galileo::block_predecessor_set;

public:
	mdb_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	galileo::transaction tx_begin_write () override;
	galileo::transaction tx_begin_read () override;
	galileo::transaction tx_begin (bool write = false) override;

	void initialize (galileo::transaction const &, galileo::genesis const &) override;
	void block_put (galileo::transaction const &, galileo::block_hash const &, galileo::block const &, galileo::block_hash const & = galileo::block_hash (0), galileo::epoch version = galileo::epoch::epoch_0) override;
	galileo::block_hash block_successor (galileo::transaction const &, galileo::block_hash const &) override;
	void block_successor_clear (galileo::transaction const &, galileo::block_hash const &) override;
	std::unique_ptr<galileo::block> block_get (galileo::transaction const &, galileo::block_hash const &) override;
	std::unique_ptr<galileo::block> block_random (galileo::transaction const &) override;
	void block_del (galileo::transaction const &, galileo::block_hash const &) override;
	bool block_exists (galileo::transaction const &, galileo::block_hash const &) override;
	galileo::block_counts block_count (galileo::transaction const &) override;
	bool root_exists (galileo::transaction const &, galileo::uint256_union const &) override;

	void frontier_put (galileo::transaction const &, galileo::block_hash const &, galileo::account const &) override;
	galileo::account frontier_get (galileo::transaction const &, galileo::block_hash const &) override;
	void frontier_del (galileo::transaction const &, galileo::block_hash const &) override;

	void account_put (galileo::transaction const &, galileo::account const &, galileo::account_info const &) override;
	bool account_get (galileo::transaction const &, galileo::account const &, galileo::account_info &) override;
	void account_del (galileo::transaction const &, galileo::account const &) override;
	bool account_exists (galileo::transaction const &, galileo::account const &) override;
	size_t account_count (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_begin (galileo::transaction const &, galileo::account const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v0_end () override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_begin (galileo::transaction const &, galileo::account const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_v1_end () override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_begin (galileo::transaction const &, galileo::account const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, galileo::account_info> latest_end () override;

	void pending_put (galileo::transaction const &, galileo::pending_key const &, galileo::pending_info const &) override;
	void pending_del (galileo::transaction const &, galileo::pending_key const &) override;
	bool pending_get (galileo::transaction const &, galileo::pending_key const &, galileo::pending_info &) override;
	bool pending_exists (galileo::transaction const &, galileo::pending_key const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_begin (galileo::transaction const &, galileo::pending_key const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v0_end () override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_begin (galileo::transaction const &, galileo::pending_key const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_v1_end () override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_begin (galileo::transaction const &, galileo::pending_key const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::pending_key, galileo::pending_info> pending_end () override;

	void block_info_put (galileo::transaction const &, galileo::block_hash const &, galileo::block_info const &) override;
	void block_info_del (galileo::transaction const &, galileo::block_hash const &) override;
	bool block_info_get (galileo::transaction const &, galileo::block_hash const &, galileo::block_info &) override;
	bool block_info_exists (galileo::transaction const &, galileo::block_hash const &) override;
	galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_begin (galileo::transaction const &, galileo::block_hash const &) override;
	galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::block_hash, galileo::block_info> block_info_end () override;
	galileo::uint128_t block_balance (galileo::transaction const &, galileo::block_hash const &) override;
	galileo::epoch block_version (galileo::transaction const &, galileo::block_hash const &) override;

	galileo::uint128_t representation_get (galileo::transaction const &, galileo::account const &) override;
	void representation_put (galileo::transaction const &, galileo::account const &, galileo::uint128_t const &) override;
	void representation_add (galileo::transaction const &, galileo::account const &, galileo::uint128_t const &) override;
	galileo::store_iterator<galileo::account, galileo::uint128_union> representation_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, galileo::uint128_union> representation_end () override;

	void unchecked_clear (galileo::transaction const &) override;
	void unchecked_put (galileo::transaction const &, galileo::block_hash const &, std::shared_ptr<galileo::block> const &) override;
	std::vector<std::shared_ptr<galileo::block>> unchecked_get (galileo::transaction const &, galileo::block_hash const &) override;
	void unchecked_del (galileo::transaction const &, galileo::block_hash const &, std::shared_ptr<galileo::block>) override;
	galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_begin (galileo::transaction const &, galileo::block_hash const &) override;
	galileo::store_iterator<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_end () override;
	size_t unchecked_count (galileo::transaction const &) override;
	std::unordered_multimap<galileo::block_hash, std::shared_ptr<galileo::block>> unchecked_cache;

	void checksum_put (galileo::transaction const &, uint64_t, uint8_t, galileo::checksum const &) override;
	bool checksum_get (galileo::transaction const &, uint64_t, uint8_t, galileo::checksum &) override;
	void checksum_del (galileo::transaction const &, uint64_t, uint8_t) override;

	// Return latest vote for an account from store
	std::shared_ptr<galileo::vote> vote_get (galileo::transaction const &, galileo::account const &) override;
	// Populate vote with the next sequence number
	std::shared_ptr<galileo::vote> vote_generate (galileo::transaction const &, galileo::account const &, galileo::raw_key const &, std::shared_ptr<galileo::block>) override;
	std::shared_ptr<galileo::vote> vote_generate (galileo::transaction const &, galileo::account const &, galileo::raw_key const &, std::vector<galileo::block_hash>) override;
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<galileo::vote> vote_max (galileo::transaction const &, std::shared_ptr<galileo::vote>) override;
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<galileo::vote> vote_current (galileo::transaction const &, galileo::account const &) override;
	void flush (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, std::shared_ptr<galileo::vote>> vote_begin (galileo::transaction const &) override;
	galileo::store_iterator<galileo::account, std::shared_ptr<galileo::vote>> vote_end () override;
	std::mutex cache_mutex;
	std::unordered_map<galileo::account, std::shared_ptr<galileo::vote>> vote_cache;

	void version_put (galileo::transaction const &, int) override;
	int version_get (galileo::transaction const &) override;
	void do_upgrades (galileo::transaction const &);
	void upgrade_v1_to_v2 (galileo::transaction const &);
	void upgrade_v2_to_v3 (galileo::transaction const &);
	void upgrade_v3_to_v4 (galileo::transaction const &);
	void upgrade_v4_to_v5 (galileo::transaction const &);
	void upgrade_v5_to_v6 (galileo::transaction const &);
	void upgrade_v6_to_v7 (galileo::transaction const &);
	void upgrade_v7_to_v8 (galileo::transaction const &);
	void upgrade_v8_to_v9 (galileo::transaction const &);
	void upgrade_v9_to_v10 (galileo::transaction const &);
	void upgrade_v10_to_v11 (galileo::transaction const &);
	void upgrade_v11_to_v12 (galileo::transaction const &);

	// Requires a write transaction
	galileo::raw_key get_node_id (galileo::transaction const &) override;

	/** Deletes the node ID from the store */
	void delete_node_id (galileo::transaction const &) override;

	galileo::mdb_env env;

	/**
	 * Maps head block to owning account
	 * galileo::block_hash -> galileo::account
	 */
	MDB_dbi frontiers;

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count.
	 * galileo::account -> galileo::block_hash, galileo::block_hash, galileo::block_hash, galileo::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0;

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count.
	 * galileo::account -> galileo::block_hash, galileo::block_hash, galileo::block_hash, galileo::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1;

	/**
	 * Maps block hash to send block.
	 * galileo::block_hash -> galileo::send_block
	 */
	MDB_dbi send_blocks;

	/**
	 * Maps block hash to receive block.
	 * galileo::block_hash -> galileo::receive_block
	 */
	MDB_dbi receive_blocks;

	/**
	 * Maps block hash to open block.
	 * galileo::block_hash -> galileo::open_block
	 */
	MDB_dbi open_blocks;

	/**
	 * Maps block hash to change block.
	 * galileo::block_hash -> galileo::change_block
	 */
	MDB_dbi change_blocks;

	/**
	 * Maps block hash to v0 state block.
	 * galileo::block_hash -> galileo::state_block
	 */
	MDB_dbi state_blocks_v0;

	/**
	 * Maps block hash to v1 state block.
	 * galileo::block_hash -> galileo::state_block
	 */
	MDB_dbi state_blocks_v1;

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount).
	 * galileo::account, galileo::block_hash -> galileo::account, galileo::amount
	 */
	MDB_dbi pending_v0;

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount).
	 * galileo::account, galileo::block_hash -> galileo::account, galileo::amount
	 */
	MDB_dbi pending_v1;

	/**
	 * Maps block hash to account and balance.
	 * block_hash -> galileo::account, galileo::amount
	 */
	MDB_dbi blocks_info;

	/**
	 * Representative weights.
	 * galileo::account -> galileo::uint128_t
	 */
	MDB_dbi representation;

	/**
	 * Unchecked bootstrap blocks.
	 * galileo::block_hash -> galileo::block
	 */
	MDB_dbi unchecked;

	/**
	 * Mapping of region to checksum.
	 * (uint56_t, uint8_t) -> galileo::block_hash
	 */
	MDB_dbi checksum;

	/**
	 * Highest vote observed for account.
	 * galileo::account -> uint64_t
	 */
	MDB_dbi vote;

	/**
	 * Meta information about block store, such as versions.
	 * galileo::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta;

private:
	MDB_dbi block_database (galileo::block_type, galileo::epoch);
	template <typename T>
	std::unique_ptr<galileo::block> block_random (galileo::transaction const &, MDB_dbi);
	MDB_val block_raw_get (galileo::transaction const &, galileo::block_hash const &, galileo::block_type &);
	void block_raw_put (galileo::transaction const &, MDB_dbi, galileo::block_hash const &, MDB_val);
	void clear (MDB_dbi);
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (galileo::mdb_val const &);
	wallet_value (galileo::uint256_union const &, uint64_t);
	galileo::mdb_val val () const;
	galileo::private_key key;
	uint64_t work;
};
}
