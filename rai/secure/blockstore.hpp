#pragma once

#include <rai/node/lmdb.hpp>
#include <rai/secure/common.hpp>

namespace rai
{
class block_store;
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual rai::store_iterator_impl<T, U> & operator++ () = 0;
	virtual bool operator== (rai::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual void next_dup () = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	rai::store_iterator_impl<T, U> & operator= (rai::store_iterator_impl<T, U> const &) = delete;
	bool operator== (rai::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (rai::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
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
	void next_dup () override;
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
template <typename T, typename U>
class mdb_merge_iterator;
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator
{
	friend class rai::block_store;
	friend class rai::mdb_merge_iterator<T, U>;

public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<rai::store_iterator_impl<T, U>> impl_a) :
	impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (rai::store_iterator<T, U> && other_a) :
	current (std::move (other_a.current)),
	impl (std::move (other_a.impl))
	{
	}
	rai::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	rai::store_iterator<T, U> & operator= (rai::store_iterator<T, U> && other_a)
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	rai::store_iterator<T, U> & operator= (rai::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (rai::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (rai::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<rai::store_iterator_impl<T, U>> impl;
};

class block_predecessor_set;

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
	void next_dup () override;
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
 * Manages block storage and iteration
 */
class block_store
{
	friend class rai::block_predecessor_set;

public:
	block_store (bool &, boost::filesystem::path const &, int lmdb_max_dbs = 128);

	rai::transaction tx_begin (bool = false);
	void initialize (rai::transaction const &, rai::genesis const &);
	void block_put (rai::transaction const &, rai::block_hash const &, rai::block const &, rai::block_hash const & = rai::block_hash (0), rai::epoch version = rai::epoch::epoch_0);
	rai::block_hash block_successor (rai::transaction const &, rai::block_hash const &);
	void block_successor_clear (rai::transaction const &, rai::block_hash const &);
	std::unique_ptr<rai::block> block_get (rai::transaction const &, rai::block_hash const &);
	std::unique_ptr<rai::block> block_random (rai::transaction const &);
	void block_del (rai::transaction const &, rai::block_hash const &);
	bool block_exists (rai::transaction const &, rai::block_hash const &);
	rai::block_counts block_count (rai::transaction const &);
	bool root_exists (rai::transaction const &, rai::uint256_union const &);

	void frontier_put (rai::transaction const &, rai::block_hash const &, rai::account const &);
	rai::account frontier_get (rai::transaction const &, rai::block_hash const &);
	void frontier_del (rai::transaction const &, rai::block_hash const &);

	void account_put (rai::transaction const &, rai::account const &, rai::account_info const &);
	bool account_get (rai::transaction const &, rai::account const &, rai::account_info &);
	void account_del (rai::transaction const &, rai::account const &);
	bool account_exists (rai::transaction const &, rai::account const &);
	size_t account_count (rai::transaction const &);
	rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &, rai::account const &);
	rai::store_iterator<rai::account, rai::account_info> latest_v0_begin (rai::transaction const &);
	rai::store_iterator<rai::account, rai::account_info> latest_v0_end ();
	rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &, rai::account const &);
	rai::store_iterator<rai::account, rai::account_info> latest_v1_begin (rai::transaction const &);
	rai::store_iterator<rai::account, rai::account_info> latest_v1_end ();
	rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &, rai::account const &);
	rai::store_iterator<rai::account, rai::account_info> latest_begin (rai::transaction const &);
	rai::store_iterator<rai::account, rai::account_info> latest_end ();

	void pending_put (rai::transaction const &, rai::pending_key const &, rai::pending_info const &);
	void pending_del (rai::transaction const &, rai::pending_key const &);
	bool pending_get (rai::transaction const &, rai::pending_key const &, rai::pending_info &);
	bool pending_exists (rai::transaction const &, rai::pending_key const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &, rai::pending_key const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_begin (rai::transaction const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v0_end ();
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &, rai::pending_key const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_begin (rai::transaction const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_v1_end ();
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &, rai::pending_key const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_begin (rai::transaction const &);
	rai::store_iterator<rai::pending_key, rai::pending_info> pending_end ();

	void block_info_put (rai::transaction const &, rai::block_hash const &, rai::block_info const &);
	void block_info_del (rai::transaction const &, rai::block_hash const &);
	bool block_info_get (rai::transaction const &, rai::block_hash const &, rai::block_info &);
	bool block_info_exists (rai::transaction const &, rai::block_hash const &);
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &, rai::block_hash const &);
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_begin (rai::transaction const &);
	rai::store_iterator<rai::block_hash, rai::block_info> block_info_end ();
	rai::uint128_t block_balance (rai::transaction const &, rai::block_hash const &);
	rai::epoch block_version (rai::transaction const &, rai::block_hash const &);
	static size_t const block_info_max = 32;

	rai::uint128_t representation_get (rai::transaction const &, rai::account const &);
	void representation_put (rai::transaction const &, rai::account const &, rai::uint128_t const &);
	void representation_add (rai::transaction const &, rai::account const &, rai::uint128_t const &);
	rai::store_iterator<rai::account, rai::uint128_union> representation_begin (rai::transaction const &);
	rai::store_iterator<rai::account, rai::uint128_union> representation_end ();

	void unchecked_clear (rai::transaction const &);
	void unchecked_put (rai::transaction const &, rai::block_hash const &, std::shared_ptr<rai::block> const &);
	std::vector<std::shared_ptr<rai::block>> unchecked_get (rai::transaction const &, rai::block_hash const &);
	void unchecked_del (rai::transaction const &, rai::block_hash const &, std::shared_ptr<rai::block>);
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &);
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> unchecked_begin (rai::transaction const &, rai::block_hash const &);
	rai::store_iterator<rai::block_hash, std::shared_ptr<rai::block>> unchecked_end ();
	size_t unchecked_count (rai::transaction const &);
	std::unordered_multimap<rai::block_hash, std::shared_ptr<rai::block>> unchecked_cache;

	void checksum_put (rai::transaction const &, uint64_t, uint8_t, rai::checksum const &);
	bool checksum_get (rai::transaction const &, uint64_t, uint8_t, rai::checksum &);
	void checksum_del (rai::transaction const &, uint64_t, uint8_t);

	// Return latest vote for an account from store
	std::shared_ptr<rai::vote> vote_get (rai::transaction const &, rai::account const &);
	// Populate vote with the next sequence number
	std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::shared_ptr<rai::block>);
	std::shared_ptr<rai::vote> vote_generate (rai::transaction const &, rai::account const &, rai::raw_key const &, std::vector<rai::block_hash>);
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<rai::vote> vote_max (rai::transaction const &, std::shared_ptr<rai::vote>);
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<rai::vote> vote_current (rai::transaction const &, rai::account const &);
	void flush (rai::transaction const &);
	rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_begin (rai::transaction const &);
	rai::store_iterator<rai::account, std::shared_ptr<rai::vote>> vote_end ();
	std::mutex cache_mutex;
	std::unordered_map<rai::account, std::shared_ptr<rai::vote>> vote_cache;

	void version_put (rai::transaction const &, int);
	int version_get (rai::transaction const &);
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
	rai::raw_key get_node_id (rai::transaction const &);

	/** Deletes the node ID from the store */
	void delete_node_id (rai::transaction const &);

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
	std::unique_ptr<rai::block> block_random (rai::transaction const &, MDB_dbi);
	MDB_val block_raw_get (rai::transaction const &, rai::block_hash const &, rai::block_type &);
	void block_raw_put (rai::transaction const &, MDB_dbi, rai::block_hash const &, MDB_val);
	void clear (MDB_dbi);
};
}
