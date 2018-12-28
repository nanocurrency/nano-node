#pragma once

#include <boost/filesystem.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

#include <rai/lib/numbers.hpp>
#include <rai/secure/blockstore.hpp>
#include <rai/secure/common.hpp>

namespace rai
{
class mdb_env;
class mdb_txn : public transaction_impl
{
public:
	mdb_txn (rai::mdb_env const &, bool = false);
	mdb_txn (rai::mdb_txn const &) = delete;
	mdb_txn (rai::mdb_txn &&) = default;
	~mdb_txn ();
	rai::mdb_txn & operator= (rai::mdb_txn const &) = delete;
	rai::mdb_txn & operator= (rai::mdb_txn &&) = default;
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
	rai::transaction tx_begin (bool = false) const;
	MDB_txn * tx (rai::transaction const &) const;
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
	mdb_val (rai::epoch = rai::epoch::unspecified);
	mdb_val (rai::account_info const &);
	mdb_val (rai::block_info const &);
	mdb_val (MDB_val const &, rai::epoch = rai::epoch::unspecified);
	mdb_val (rai::pending_info const &);
	mdb_val (rai::pending_key const &);
	mdb_val (size_t, void *);
	mdb_val (rai::uint128_union const &);
	mdb_val (rai::uint256_union const &);
	mdb_val (std::shared_ptr<rai::block> const &);
	mdb_val (std::shared_ptr<rai::vote> const &);
	void * data () const;
	size_t size () const;
	explicit operator rai::account_info () const;
	explicit operator rai::block_info () const;
	explicit operator rai::pending_info () const;
	explicit operator rai::pending_key () const;
	explicit operator rai::uint128_union () const;
	explicit operator rai::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator no_value () const;
	explicit operator std::shared_ptr<rai::block> () const;
	explicit operator std::shared_ptr<rai::send_block> () const;
	explicit operator std::shared_ptr<rai::receive_block> () const;
	explicit operator std::shared_ptr<rai::open_block> () const;
	explicit operator std::shared_ptr<rai::change_block> () const;
	explicit operator std::shared_ptr<rai::state_block> () const;
	explicit operator std::shared_ptr<rai::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	rai::epoch epoch{ rai::epoch::unspecified };
};
class block_store;

template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (rai::transaction const & transaction_a, MDB_dbi db_a, rai::epoch = rai::epoch::unspecified);
	mdb_iterator (std::nullptr_t, rai::epoch = rai::epoch::unspecified);
	mdb_iterator (rai::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, rai::epoch = rai::epoch::unspecified);
	mdb_iterator (rai::mdb_iterator<T, U> && other_a);
	mdb_iterator (rai::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	rai::store_iterator_impl<T, U> & operator++ () override;
	std::pair<rai::mdb_val, rai::mdb_val> * operator-> ();
	bool operator== (rai::store_iterator_impl<T, U> const & other_a) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	rai::mdb_iterator<T, U> & operator= (rai::mdb_iterator<T, U> && other_a);
	rai::store_iterator_impl<T, U> & operator= (rai::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<rai::mdb_val, rai::mdb_val> current;

private:
	MDB_txn * tx (rai::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (rai::transaction const &, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (rai::transaction const &, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (rai::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (rai::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	rai::store_iterator_impl<T, U> & operator++ () override;
	std::pair<rai::mdb_val, rai::mdb_val> * operator-> ();
	bool operator== (rai::store_iterator_impl<T, U> const &) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	rai::mdb_merge_iterator<T, U> & operator= (rai::mdb_merge_iterator<T, U> &&) = default;
	rai::mdb_merge_iterator<T, U> & operator= (rai::mdb_merge_iterator<T, U> const &) = delete;

private:
	rai::mdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<rai::mdb_iterator<T, U>> impl1;
	std::unique_ptr<rai::mdb_iterator<T, U>> impl2;
};

/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store
{
	friend class rai::block_predecessor_set;

public:
	mdb_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	rai::transaction tx_begin_write () override;
	rai::transaction tx_begin_read () override;
	rai::transaction tx_begin (bool write = false) override;

	void initialize (rai::transaction const &, rai::genesis const &) override;
	void block_put (rai::transaction const &, rai::block_hash const &, rai::block const &, rai::block_hash const & = rai::block_hash (0), rai::epoch version = rai::epoch::epoch_0) override;
	rai::block_hash block_successor (rai::transaction const &, rai::block_hash const &) override;
	void block_successor_clear (rai::transaction const &, rai::block_hash const &) override;
	std::shared_ptr<rai::block> block_get (rai::transaction const &, rai::block_hash const &) override;
	std::shared_ptr<rai::block> block_random (rai::transaction const &) override;
	void block_del (rai::transaction const &, rai::block_hash const &) override;
	bool block_exists (rai::transaction const &, rai::block_hash const &) override;
	bool block_exists (rai::transaction const &, rai::block_type, rai::block_hash const &) override;
	rai::block_counts block_count (rai::transaction const &) override;
	bool root_exists (rai::transaction const &, rai::uint256_union const &) override;

	void frontier_put (rai::transaction const &, rai::block_hash const &, rai::account const &) override;
	rai::account frontier_get (rai::transaction const &, rai::block_hash const &) override;
	void frontier_del (rai::transaction const &, rai::block_hash const &) override;

	void account_put (rai::transaction const &, rai::account const &, rai::account_info const &) override;
	bool account_get (rai::transaction const &, rai::account const &, rai::account_info &) override;
	void account_del (rai::transaction const &, rai::account const &) override;
	bool account_exists (rai::transaction const &, rai::account const &) override;
	size_t account_count (rai::transaction const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &, rai::account const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_v0_end () override;
	rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &, rai::account const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_v1_end () override;
	rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &, rai::account const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &) override;
	rai::store_iterator<rai::account, rai::account_info> latest_end () override;

	void pending_put (rai::transaction const &, rai::pending_key const &, rai::pending_info const &) override;
	void pending_del (rai::transaction const &, rai::pending_key const &) override;
	bool pending_get (rai::transaction const &, rai::pending_key const &, rai::pending_info &) override;
	bool pending_exists (rai::transaction const &, rai::pending_key const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &, rai::pending_key const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_end () override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &, rai::pending_key const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_end () override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &, rai::pending_key const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &) override;
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_end () override;

	void block_info_put (rai::transaction const &, rai::block_hash const &, rai::block_info const &) override;
	void block_info_del (rai::transaction const &, rai::block_hash const &) override;
	bool block_info_get (rai::transaction const &, rai::block_hash const &, rai::block_info &) override;
	bool block_info_exists (rai::transaction const &, rai::block_hash const &) override;
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &, rai::block_hash const &) override;
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &) override;
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_end () override;
	rai::uint128_t block_balance (rai::transaction const &, rai::block_hash const &) override;
	rai::epoch block_version (rai::transaction const &, rai::block_hash const &) override;

	rai::uint128_t representation_get (rai::transaction const &, rai::account const &) override;
	void representation_put (rai::transaction const &, rai::account const &, rai::uint128_t const &) override;
	void representation_add (rai::transaction const &, rai::account const &, rai::uint128_t const &) override;
	rai::store_iterator<rai::account, rai::uint128_union> representation_begin (rai::transaction const &) override;
	rai::store_iterator<rai::account, rai::uint128_union> representation_end () override;

	void unchecked_clear (rai::transaction const &) override;
	void unchecked_put (rai::transaction const &, rai::unchecked_key const &, std::shared_ptr<rai::block> const &) override;
	void unchecked_put (rai::transaction const &, rai::block_hash const &, std::shared_ptr<rai::block> const &) override;
	std::vector<std::shared_ptr<rai::block>> unchecked_get (rai::transaction const &, rai::block_hash const &) override;
	bool unchecked_exists (rai::transaction const &, rai::unchecked_key const &) override;
	void unchecked_del (rai::transaction const &, rai::unchecked_key const &) override;
	rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &) override;
	rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &, rai::unchecked_key const &) override;
	rai::store_iterator<rai::unchecked_key, std::shared_ptr<rai::block>> unchecked_end () override;
	size_t unchecked_count (rai::transaction const &) override;

	void checksum_put (rai::transaction const &, uint64_t, uint8_t, rai::checksum const &) override;
	bool checksum_get (rai::transaction const &, uint64_t, uint8_t, rai::checksum &) override;
	void checksum_del (rai::transaction const &, uint64_t, uint8_t) override;

	// Return latest vote for an account from store
	std::shared_ptr<rai::vote> vote_get (rai::transaction const &, rai::account const &) override;
	// Populate vote with the next sequence number
	std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::shared_ptr<rai::block>) override;
	std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::vector<rai::block_hash>) override;
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<rai::vote> vote_max (rai::transaction const &, std::shared_ptr<rai::vote>) override;
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<rai::vote> vote_current (rai::transaction const &, rai::account const &) override;
	void flush (rai::transaction const &) override;
	rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_begin (rai::transaction const &) override;
	rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_end () override;
	std::mutex cache_mutex;
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> vote_cache_l1;
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> vote_cache_l2;

	void version_put (rai::transaction const &, int) override;
	int version_get (rai::transaction const &) override;
	void do_upgrades (rai::transaction const &);
	void upgrade_v1_to_v2 (rai::transaction const &);
	void upgrade_v2_to_v3 (rai::transaction const &);
	void upgrade_v3_to_v4 (rai::transaction const &);
	void upgrade_v4_to_v5 (rai::transaction const &);
	void upgrade_v5_to_v6 (rai::transaction const &);
	void upgrade_v6_to_v7 (rai::transaction const &);
	void upgrade_v7_to_v8 (rai::transaction const &);
	void upgrade_v8_to_v9 (rai::transaction const &);
	void upgrade_v9_to_v10 (rai::transaction const &);
	void upgrade_v10_to_v11 (rai::transaction const &);
	void upgrade_v11_to_v12 (rai::transaction const &);

	// Requires a write transaction
	rai::raw_key get_node_id (rai::transaction const &) override;

	/** Deletes the node ID from the store */
	void delete_node_id (rai::transaction const &) override;

	rai::mdb_env env;

	/**
	 * Maps head block to owning account
	 * rai::block_hash -> rai::account
	 */
	MDB_dbi frontiers;

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count.
	 * rai::account -> rai::block_hash, rai::block_hash, rai::block_hash, rai::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0;

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count.
	 * rai::account -> rai::block_hash, rai::block_hash, rai::block_hash, rai::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1;

	/**
	 * Maps block hash to send block.
	 * rai::block_hash -> rai::send_block
	 */
	MDB_dbi send_blocks;

	/**
	 * Maps block hash to receive block.
	 * rai::block_hash -> rai::receive_block
	 */
	MDB_dbi receive_blocks;

	/**
	 * Maps block hash to open block.
	 * rai::block_hash -> rai::open_block
	 */
	MDB_dbi open_blocks;

	/**
	 * Maps block hash to change block.
	 * rai::block_hash -> rai::change_block
	 */
	MDB_dbi change_blocks;

	/**
	 * Maps block hash to v0 state block.
	 * rai::block_hash -> rai::state_block
	 */
	MDB_dbi state_blocks_v0;

	/**
	 * Maps block hash to v1 state block.
	 * rai::block_hash -> rai::state_block
	 */
	MDB_dbi state_blocks_v1;

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount).
	 * rai::account, rai::block_hash -> rai::account, rai::amount
	 */
	MDB_dbi pending_v0;

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount).
	 * rai::account, rai::block_hash -> rai::account, rai::amount
	 */
	MDB_dbi pending_v1;

	/**
	 * Maps block hash to account and balance.
	 * block_hash -> rai::account, rai::amount
	 */
	MDB_dbi blocks_info;

	/**
	 * Representative weights.
	 * rai::account -> rai::uint128_t
	 */
	MDB_dbi representation;

	/**
	 * Unchecked bootstrap blocks.
	 * rai::block_hash -> rai::block
	 */
	MDB_dbi unchecked;

	/**
	 * Mapping of region to checksum.
	 * (uint56_t, uint8_t) -> rai::block_hash
	 */
	MDB_dbi checksum;

	/**
	 * Highest vote observed for account.
	 * rai::account -> uint64_t
	 */
	MDB_dbi vote;

	/**
	 * Meta information about block store, such as versions.
	 * rai::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta;

private:
	MDB_dbi block_database (rai::block_type, rai::epoch);
	template <typename T>
	std::shared_ptr<rai::block> block_random (rai::transaction const &, MDB_dbi);
	MDB_val block_raw_get (rai::transaction const &, rai::block_hash const &, rai::block_type &);
	void block_raw_put (rai::transaction const &, MDB_dbi, rai::block_hash const &, MDB_val);
	void clear (MDB_dbi);
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (rai::mdb_val const &);
	wallet_value (rai::uint256_union const &, uint64_t);
	rai::mdb_val val () const;
	rai::private_key key;
	uint64_t work;
};
}
