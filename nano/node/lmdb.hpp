#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/diagnosticsconfig.hpp>
#include <nano/node/lmdb_txn_tracker.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <thread>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
class mdb_env;
class account_info_v13;

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

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
	mdb_val (nano::epoch = nano::epoch::unspecified);
	mdb_val (nano::account_info const &);
	mdb_val (nano::account_info_v13 const &);
	mdb_val (nano::block_info const &);
	mdb_val (MDB_val const &, nano::epoch = nano::epoch::unspecified);
	mdb_val (nano::pending_info const &);
	mdb_val (nano::pending_key const &);
	mdb_val (nano::unchecked_info const &);
	mdb_val (size_t, void *);
	mdb_val (nano::uint128_union const &);
	mdb_val (nano::uint256_union const &);
	mdb_val (nano::endpoint_key const &);
	mdb_val (std::shared_ptr<nano::block> const &);
	mdb_val (std::shared_ptr<nano::vote> const &);
	mdb_val (uint64_t);
	void * data () const;
	size_t size () const;
	explicit operator nano::account_info () const;
	explicit operator nano::account_info_v13 () const;
	explicit operator nano::block_info () const;
	explicit operator nano::pending_info () const;
	explicit operator nano::pending_key () const;
	explicit operator nano::unchecked_info () const;
	explicit operator nano::uint128_union () const;
	explicit operator nano::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator nano::endpoint_key () const;
	explicit operator nano::no_value () const;
	explicit operator std::shared_ptr<nano::block> () const;
	explicit operator std::shared_ptr<nano::send_block> () const;
	explicit operator std::shared_ptr<nano::receive_block> () const;
	explicit operator std::shared_ptr<nano::open_block> () const;
	explicit operator std::shared_ptr<nano::change_block> () const;
	explicit operator std::shared_ptr<nano::state_block> () const;
	explicit operator std::shared_ptr<nano::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	nano::epoch epoch{ nano::epoch::unspecified };
};
class block_store;

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
class mdb_store : public block_store
{
	friend class nano::block_predecessor_set;

public:
	mdb_store (bool &, nano::logger_mt &, boost::filesystem::path const &, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, bool drop_unchecked = false, size_t batch_size = 512);
	nano::write_transaction tx_begin_write () override;
	nano::read_transaction tx_begin_read () override;

	void initialize (nano::transaction const &, nano::genesis const &) override;
	void block_put (nano::transaction const &, nano::block_hash const &, nano::block const &, nano::block_sideband const &, nano::epoch version = nano::epoch::epoch_0) override;
	nano::block_hash block_successor (nano::transaction const &, nano::block_hash const &) const override;
	void block_successor_clear (nano::transaction const &, nano::block_hash const &) override;
	std::shared_ptr<nano::block> block_get (nano::transaction const &, nano::block_hash const &, nano::block_sideband * = nullptr) const override;
	std::shared_ptr<nano::block> block_random (nano::transaction const &) override;
	void block_del (nano::transaction const &, nano::block_hash const &) override;
	bool block_exists (nano::transaction const &, nano::block_hash const &) override;
	bool block_exists (nano::transaction const &, nano::block_type, nano::block_hash const &) override;
	nano::block_counts block_count (nano::transaction const &) override;
	bool root_exists (nano::transaction const &, nano::uint256_union const &) override;
	bool source_exists (nano::transaction const &, nano::block_hash const &) override;
	nano::account block_account (nano::transaction const &, nano::block_hash const &) const override;

	void frontier_put (nano::transaction const &, nano::block_hash const &, nano::account const &) override;
	nano::account frontier_get (nano::transaction const &, nano::block_hash const &) const override;
	void frontier_del (nano::transaction const &, nano::block_hash const &) override;

	void account_put (nano::transaction const &, nano::account const &, nano::account_info const &) override;
	bool account_get (nano::transaction const &, nano::account const &, nano::account_info &) override;
	void account_del (nano::transaction const &, nano::account const &) override;
	bool account_exists (nano::transaction const &, nano::account const &) override;
	size_t account_count (nano::transaction const &) override;
	void confirmation_height_clear (nano::transaction const &, nano::account const & account, nano::account_info const & account_info) override;
	void confirmation_height_clear (nano::transaction const &) override;
	uint64_t cemented_count (nano::transaction const &) override;
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
	bool pending_exists (nano::transaction const &, nano::pending_key const &) override;
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
	nano::uint128_t block_balance (nano::transaction const &, nano::block_hash const &) override;
	nano::epoch block_version (nano::transaction const &, nano::block_hash const &) override;

	nano::uint128_t representation_get (nano::transaction const &, nano::account const &) override;
	void representation_put (nano::transaction const &, nano::account const &, nano::uint128_t const &) override;
	void representation_add (nano::transaction const &, nano::account const &, nano::uint128_t const &) override;
	nano::store_iterator<nano::account, nano::uint128_union> representation_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, nano::uint128_union> representation_end () override;

	void unchecked_clear (nano::transaction const &) override;
	void unchecked_put (nano::transaction const &, nano::unchecked_key const &, nano::unchecked_info const &) override;
	void unchecked_put (nano::transaction const &, nano::block_hash const &, std::shared_ptr<nano::block> const &) override;
	std::vector<nano::unchecked_info> unchecked_get (nano::transaction const &, nano::block_hash const &) override;
	void unchecked_del (nano::transaction const &, nano::unchecked_key const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const &, nano::unchecked_key const &) override;
	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_end () override;
	size_t unchecked_count (nano::transaction const &) override;

	// Return latest vote for an account from store
	std::shared_ptr<nano::vote> vote_get (nano::transaction const &, nano::account const &) override;
	// Populate vote with the next sequence number
	std::shared_ptr<nano::vote> vote_generate (nano::transaction const &, nano::account const &, nano::raw_key const &, std::shared_ptr<nano::block>) override;
	std::shared_ptr<nano::vote> vote_generate (nano::transaction const &, nano::account const &, nano::raw_key const &, std::vector<nano::block_hash>) override;
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<nano::vote> vote_max (nano::transaction const &, std::shared_ptr<nano::vote>) override;
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<nano::vote> vote_current (nano::transaction const &, nano::account const &) override;
	void flush (nano::transaction const &) override;
	nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_begin (nano::transaction const &) override;
	nano::store_iterator<nano::account, std::shared_ptr<nano::vote>> vote_end () override;

	void online_weight_put (nano::transaction const &, uint64_t, nano::amount const &) override;
	void online_weight_del (nano::transaction const &, uint64_t) override;
	nano::store_iterator<uint64_t, nano::amount> online_weight_begin (nano::transaction const &) override;
	nano::store_iterator<uint64_t, nano::amount> online_weight_end () override;
	size_t online_weight_count (nano::transaction const &) const override;
	void online_weight_clear (nano::transaction const &) override;

	std::mutex cache_mutex;
	std::unordered_map<nano::account, std::shared_ptr<nano::vote>> vote_cache_l1;
	std::unordered_map<nano::account, std::shared_ptr<nano::vote>> vote_cache_l2;

	void version_put (nano::transaction const &, int) override;
	int version_get (nano::transaction const &) const override;

	void peer_put (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	bool peer_exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override;
	void peer_del (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override;
	size_t peer_count (nano::transaction const & transaction_a) const override;
	void peer_clear (nano::transaction const & transaction_a) override;

	nano::store_iterator<nano::endpoint_key, nano::no_value> peers_begin (nano::transaction const & transaction_a) override;
	nano::store_iterator<nano::endpoint_key, nano::no_value> peers_end () override;

	uint64_t block_account_height (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override;

	bool full_sideband (nano::transaction const &) const;
	MDB_dbi get_account_db (nano::epoch epoch_a) const;
	size_t block_successor_offset (nano::transaction const &, MDB_val, nano::block_type) const;
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

private:
	nano::network_params network_params;
	bool entry_has_sideband (MDB_val, nano::block_type) const;
	nano::account block_account_computed (nano::transaction const &, nano::block_hash const &) const;
	nano::uint128_t block_balance_computed (nano::transaction const &, nano::block_hash const &) const;
	MDB_dbi block_database (nano::block_type, nano::epoch);
	template <typename T>
	std::shared_ptr<nano::block> block_random (nano::transaction const &, MDB_dbi);
	MDB_val block_raw_get (nano::transaction const &, nano::block_hash const &, nano::block_type &) const;
	boost::optional<MDB_val> block_raw_get_by_type (nano::transaction const &, nano::block_hash const &, nano::block_type &) const;
	void block_raw_put (nano::transaction const &, MDB_dbi, nano::block_hash const &, MDB_val);
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
	void upgrade_v9_to_v10 (nano::transaction const &);
	void upgrade_v10_to_v11 (nano::transaction const &);
	void upgrade_v11_to_v12 (nano::transaction const &);
	void upgrade_v12_to_v13 (nano::write_transaction &, size_t);
	void upgrade_v13_to_v14 (nano::transaction const &);
	MDB_dbi get_pending_db (nano::epoch epoch_a) const;
	void open_databases (bool &, nano::transaction const &, unsigned);
	nano::mdb_txn_tracker mdb_txn_tracker;
	nano::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;
	static int constexpr version{ 14 };
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
