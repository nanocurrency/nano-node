#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/diagnosticsconfig.hpp>
#include <nano/node/lmdb_txn_tracker.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <thread>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
class mdb_env;

class mdb_txn_callbacks
{
public:
	// clang-format off
	std::function<void (const nano::transaction_impl *)> txn_start{ [] (const nano::transaction_impl *) {} };
	std::function<void (const nano::transaction_impl *)> txn_end{ [] (const nano::transaction_impl *) {} };
	// clang-format on
};

class read_mdb_txn final : public read_transaction_impl
{
public:
	read_mdb_txn (nano::mdb_env const &, mdb_txn_callbacks mdb_txn_callbacks);
	~read_mdb_txn ();
	void reset () const override;
	void renew () const override;
	void * get_handle () const override;
	MDB_txn * handle;
	mdb_txn_callbacks txn_callbacks;
};

class write_mdb_txn final : public write_transaction_impl
{
public:
	write_mdb_txn (nano::mdb_env const &, mdb_txn_callbacks mdb_txn_callbacks);
	~write_mdb_txn ();
	void commit () const override;
	void renew () override;
	void * get_handle () const override;
	MDB_txn * handle;
	nano::mdb_env const & env;
	mdb_txn_callbacks txn_callbacks;
};
/**
 * RAII wrapper for MDB_env
 */
class mdb_env final
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128, bool use_no_mem_init = false, size_t map_size = 128ULL * 1024 * 1024 * 1024);
	~mdb_env ();
	operator MDB_env * () const;
	// clang-format off
	nano::read_transaction tx_begin_read (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	nano::write_transaction tx_begin_write (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	MDB_txn * tx (nano::transaction const & transaction_a) const;
	// clang-format on
	MDB_env * environment;
};

using mdb_val = db_val<MDB_val>;

template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (nano::transaction const & transaction_a, MDB_dbi db_a, nano::epoch = nano::epoch::unspecified);
	mdb_iterator (std::nullptr_t, nano::epoch = nano::epoch::unspecified);
	mdb_iterator (nano::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, nano::epoch = nano::epoch::unspecified);
	mdb_iterator (nano::mdb_iterator<T, U> && other_a);
	mdb_iterator (nano::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	nano::store_iterator_impl<T, U> & operator++ () override;
	std::pair<nano::mdb_val, nano::mdb_val> * operator-> ();
	bool operator== (nano::store_iterator_impl<T, U> const & other_a) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	nano::mdb_iterator<T, U> & operator= (nano::mdb_iterator<T, U> && other_a);
	nano::store_iterator_impl<T, U> & operator= (nano::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<nano::mdb_val, nano::mdb_val> current;

private:
	MDB_txn * tx (nano::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (nano::transaction const &, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (nano::transaction const &, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (nano::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (nano::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	nano::store_iterator_impl<T, U> & operator++ () override;
	std::pair<nano::mdb_val, nano::mdb_val> * operator-> ();
	bool operator== (nano::store_iterator_impl<T, U> const &) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	nano::mdb_merge_iterator<T, U> & operator= (nano::mdb_merge_iterator<T, U> &&) = default;
	nano::mdb_merge_iterator<T, U> & operator= (nano::mdb_merge_iterator<T, U> const &) = delete;

private:
	nano::mdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<nano::mdb_iterator<T, U>> impl1;
	std::unique_ptr<nano::mdb_iterator<T, U>> impl2;
};

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store_partial<MDB_val>
{
public:
	using block_store_partial::block_exists;
	using block_store_partial::unchecked_put;

	mdb_store (bool &, nano::logger_mt &, boost::filesystem::path const &, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, bool drop_unchecked = false, size_t batch_size = 512);
	nano::write_transaction tx_begin_write () override;
	nano::read_transaction tx_begin_read () override;

	std::shared_ptr<nano::block> block_random (nano::transaction const &) override;
	void block_del (nano::transaction const &, nano::block_hash const &) override;
	bool block_exists (nano::transaction const &, nano::block_type, nano::block_hash const &) override;
	nano::block_counts block_count (nano::transaction const &) override;

	void frontier_put (nano::transaction const &, nano::block_hash const &, nano::account const &) override;
	nano::account frontier_get (nano::transaction const &, nano::block_hash const &) const override;
	void frontier_del (nano::transaction const &, nano::block_hash const &) override;

	void account_put (nano::transaction const &, nano::account const &, nano::account_info const &) override;
	bool account_get (nano::transaction const &, nano::account const &, nano::account_info &) override;
	void account_del (nano::transaction const &, nano::account const &) override;
	size_t account_count (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_v0_begin (nano::transaction const &, nano::account const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_v0_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_v0_end () override;
	nano::store_iterator<nano::account, nano::account_info> latest_v1_begin (nano::transaction const &, nano::account const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_v1_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_v1_end () override;
	nano::store_iterator<nano::account, nano::account_info> latest_begin (nano::transaction const &, nano::account const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::account_info> latest_end () override;

	void pending_put (nano::transaction const &, nano::pending_key const &, nano::pending_info const &) override;
	void pending_del (nano::transaction const &, nano::pending_key const &) override;
	bool pending_get (nano::transaction const &, nano::pending_key const &, nano::pending_info &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_begin (nano::transaction const &, nano::pending_key const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_begin (nano::transaction const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v0_end () override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_begin (nano::transaction const &, nano::pending_key const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_begin (nano::transaction const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_v1_end () override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &, nano::pending_key const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_begin (nano::transaction const &) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> pending_end () override;

	bool block_info_get (nano::transaction const &, nano::block_hash const &, nano::block_info &) const override;
	nano::epoch block_version (nano::transaction const &, nano::block_hash const &) override;

	nano::uint128_t representation_get (nano::transaction const &, nano::account const &) override;
	void representation_put (nano::transaction const &, nano::account const &, nano::uint128_t const &) override;
	nano::store_iterator<nano::account, nano::uint128_union> representation_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::uint128_union> representation_end () override;

	void unchecked_clear (nano::transaction const &) override;
	void unchecked_put (nano::transaction const &, nano::unchecked_key const &, nano::unchecked_info const &) override;
	void unchecked_del (nano::transaction const &, nano::unchecked_key const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &, nano::unchecked_key const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_end () override;
	size_t unchecked_count (nano::transaction const &) override;

	// Return latest vote for an account from store
	std::shared_ptr<nano::vote> vote_get (nano::transaction const &, nano::account const &) override;
	void flush (nano::transaction const &) override;
	nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_end () override;

	void online_weight_put (nano::transaction const &, uint64_t, nano::amount const &) override;
	void online_weight_del (nano::transaction const &, uint64_t) override;
	nano::store_iterator<uint64_t, nano::amount> online_weight_begin (nano::transaction const &) override;
	nano::store_iterator<uint64_t, nano::amount> online_weight_end () override;
	size_t online_weight_count (nano::transaction const &) const override;
	void online_weight_clear (nano::transaction const &) override;

	void version_put (nano::transaction const &, int) override;
	int version_get (nano::transaction const &) const override;

	void peer_put (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	bool peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override;
	void peer_del (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	size_t peer_count (nano::transaction const & transaction_a) const override;
	void peer_clear (nano::transaction const & transaction_a) override;

	nano::store_iterator<nano::endpoint_key, nano::no_value> peers_begin (nano::transaction const & transaction_a) override;
	nano::store_iterator<nano::endpoint_key, nano::no_value> peers_end () override;

	void confirmation_height_put (nano::transaction const & transaction_a, nano::account const & account_a, uint64_t confirmation_height_a) override;
	bool confirmation_height_get (nano::transaction const & transaction_a, nano::account const & account_a, uint64_t & confirmation_height_a) override;
	void confirmation_height_del (nano::transaction const & transaction_a, nano::account const & account_a) override;
	bool confirmation_height_exists (nano::transaction const & transaction_a, nano::account const & account_a) override;
	uint64_t confirmation_height_count (nano::transaction const & transaction_a) override;

	nano::store_iterator<nano::account, uint64_t> confirmation_height_begin (nano::transaction const & transaction_a, nano::account const & account_a) override;
	nano::store_iterator<nano::account, uint64_t> confirmation_height_begin (nano::transaction const & transaction_a) override;
	nano::store_iterator<nano::account, uint64_t> confirmation_height_end () override;

	MDB_dbi get_account_db (nano::epoch epoch_a) const;
	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	nano::logger_mt & logger;

	nano::mdb_env env;

	/**
	 * Maps head block to owning account
	 * nano::block_hash -> nano::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count.
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count.
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps block hash to send block.
	 * nano::block_hash -> nano::send_block
	 */
	MDB_dbi send_blocks{ 0 };

	/**
	 * Maps block hash to receive block.
	 * nano::block_hash -> nano::receive_block
	 */
	MDB_dbi receive_blocks{ 0 };

	/**
	 * Maps block hash to open block.
	 * nano::block_hash -> nano::open_block
	 */
	MDB_dbi open_blocks{ 0 };

	/**
	 * Maps block hash to change block.
	 * nano::block_hash -> nano::change_block
	 */
	MDB_dbi change_blocks{ 0 };

	/**
	 * Maps block hash to v0 state block.
	 * nano::block_hash -> nano::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block.
	 * nano::block_hash -> nano::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount).
	 * nano::account, nano::block_hash -> nano::account, nano::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount).
	 * nano::account, nano::block_hash -> nano::account, nano::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps block hash to account and balance.
	 * block_hash -> nano::account, nano::amount
	 */
	MDB_dbi blocks_info{ 0 };

	/**
	 * Representative weights.
	 * nano::account -> nano::uint128_t
	 */
	MDB_dbi representation{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * nano::block_hash -> nano::unchecked_info
	 */
	MDB_dbi unchecked{ 0 };

	/**
	 * Highest vote observed for account.
	 * nano::account -> uint64_t
	 */
	MDB_dbi vote{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> nano::amount
	 */
	MDB_dbi online_weight{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * nano::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta{ 0 };

	/*
	 * Endpoints for peers
	 * nano::endpoint_key -> no_value
	 */
	MDB_dbi peers{ 0 };

	/*
	 * Confirmation height of an account
	 * nano::account -> uint64_t
	 */
	MDB_dbi confirmation_height{ 0 };

private:
	MDB_dbi block_database (nano::block_type, nano::epoch);
	template <typename T>
	std::shared_ptr<nano::block> block_random (nano::transaction const &, MDB_dbi);
	boost::optional<DB_val> block_raw_get_by_type (nano::transaction const &, nano::block_hash const &, nano::block_type &) const override;
	void block_raw_put (nano::transaction const & transaction_a, std::vector<uint8_t> const & data, nano::block_type block_type_a, nano::epoch epoch_a, nano::block_hash const & hash_a) override;
	void clear (MDB_dbi);
	bool do_upgrades (nano::write_transaction &, size_t);
	void upgrade_v1_to_v2 (nano::transaction const &);
	void upgrade_v2_to_v3 (nano::transaction const &);
	void upgrade_v3_to_v4 (nano::transaction const &);
	void upgrade_v4_to_v5 (nano::transaction const &);
	void upgrade_v5_to_v6 (nano::transaction const &);
	void upgrade_v6_to_v7 (nano::transaction const &);
	void upgrade_v7_to_v8 (nano::transaction const &);
	void upgrade_v8_to_v9 (nano::transaction const &);
	void upgrade_v10_to_v11 (nano::transaction const &);
	void upgrade_v11_to_v12 (nano::transaction const &);
	void upgrade_v12_to_v13 (nano::write_transaction &, size_t);
	void upgrade_v13_to_v14 (nano::transaction const &);
	void upgrade_v14_to_v15 (nano::transaction const &);
	MDB_dbi get_pending_db (nano::epoch epoch_a) const;
	void open_databases (bool &, nano::transaction const &, unsigned);
	nano::mdb_txn_tracker mdb_txn_tracker;
	nano::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;
	static int constexpr version{ 15 };

	size_t count (nano::transaction const &, MDB_dbi) const;
	size_t count (nano::transaction const &, std::initializer_list<MDB_dbi>) const;
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (nano::mdb_val const &);
	wallet_value (nano::uint256_union const &, uint64_t);
	nano::mdb_val val () const;
	nano::private_key key;
	uint64_t work;
};
}
