#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/diagnosticsconfig.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/lmdb/lmdb_env.hpp>
#include <nano/node/lmdb/lmdb_iterator.hpp>
#include <nano/node/lmdb/lmdb_txn.hpp>
#include <nano/secure/blockstore_partial.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/versioning.hpp>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <thread>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store_partial<MDB_val, mdb_store>
{
public:
	using block_store_partial::block_exists;
	using block_store_partial::unchecked_put;

	mdb_store (nano::logger_mt &, boost::filesystem::path const &, nano::txn_tracking_config const & txn_tracking_config_a = nano::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, size_t batch_size = 512, bool backup_before_upgrade = false);
	nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_requiring_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) override;
	nano::read_transaction tx_begin_read () override;

	bool block_info_get (nano::transaction const &, nano::block_hash const &, nano::block_info &) const override;

	void version_put (nano::write_transaction const &, int) override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (nano::mdb_env &, boost::filesystem::path const &, nano::logger_mt &);

private:
	nano::logger_mt & logger;
	bool error{ false };

public:
	nano::mdb_env env;

	/**
	 * Maps head block to owning account
	 * nano::block_hash -> nano::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch. (Removed)
	 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t, nano::epoch
	 */
	MDB_dbi accounts{ 0 };

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
	 * Maps block hash to v0 state block. (Removed)
	 * nano::block_hash -> nano::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block. (Removed)
	 * nano::block_hash -> nano::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps block hash to state block.
	 * nano::block_hash -> nano::state_block
	 */
	MDB_dbi state_blocks{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * nano::account, nano::block_hash -> nano::account, nano::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * nano::account, nano::block_hash -> nano::account, nano::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * nano::account, nano::block_hash -> nano::account, nano::amount, nano::epoch
	 */
	MDB_dbi pending{ 0 };

	/**
	 * Maps block hash to account and balance. (Removed)
	 * block_hash -> nano::account, nano::amount
	 */
	MDB_dbi blocks_info{ 0 };

	/**
	 * Representative weights. (Removed)
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

	bool exists (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const;

	int get (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, nano::mdb_val & value_a) const;
	int put (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a, const nano::mdb_val & value_a) const;
	int del (nano::write_transaction const & transaction_a, tables table_a, nano::mdb_val const & key_a) const;

	bool copy_db (boost::filesystem::path const & destination_file) override;

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a)));
	}

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::mdb_val const & key) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	bool init_error () const override;

	size_t count (nano::transaction const &, MDB_dbi) const;

	// These are only use in the upgrade process.
	std::shared_ptr<nano::block> block_get_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const override;
	bool entry_has_sideband_v14 (size_t entry_size_a, nano::block_type type_a) const;
	size_t block_successor_offset_v14 (nano::transaction const & transaction_a, size_t entry_size_a, nano::block_type type_a) const;
	nano::block_hash block_successor_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
	nano::mdb_val block_raw_get_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a, bool * is_state_v1 = nullptr) const;
	boost::optional<nano::mdb_val> block_raw_get_by_type_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::block_type & type_a, bool * is_state_v1) const;
	nano::account block_account_computed_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
	nano::account block_account_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;
	nano::uint128_t block_balance_computed_v14 (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const;

private:
	bool do_upgrades (nano::write_transaction &, size_t);
	void upgrade_v1_to_v2 (nano::write_transaction const &);
	void upgrade_v2_to_v3 (nano::write_transaction const &);
	void upgrade_v3_to_v4 (nano::write_transaction const &);
	void upgrade_v4_to_v5 (nano::write_transaction const &);
	void upgrade_v5_to_v6 (nano::write_transaction const &);
	void upgrade_v6_to_v7 (nano::write_transaction const &);
	void upgrade_v7_to_v8 (nano::write_transaction const &);
	void upgrade_v8_to_v9 (nano::write_transaction const &);
	void upgrade_v10_to_v11 (nano::write_transaction const &);
	void upgrade_v11_to_v12 (nano::write_transaction const &);
	void upgrade_v12_to_v13 (nano::write_transaction &, size_t);
	void upgrade_v13_to_v14 (nano::write_transaction const &);
	void upgrade_v14_to_v15 (nano::write_transaction const &);
	void open_databases (bool &, nano::transaction const &, unsigned);

	int drop (nano::write_transaction const & transaction_a, tables table_a) override;
	int clear (nano::write_transaction const & transaction_a, MDB_dbi handle_a);

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;

	MDB_dbi table_to_dbi (tables table_a) const;

	nano::mdb_txn_tracker mdb_txn_tracker;
	nano::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;

	size_t count (nano::transaction const & transaction_a, tables table_a) const override;

	class upgrade_counters
	{
	public:
		upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
		bool are_equal () const;

		uint64_t before_v0;
		uint64_t before_v1;
		uint64_t after_v0{ 0 };
		uint64_t after_v1{ 0 };
	};
};

template <>
void * mdb_val::data () const;
template <>
size_t mdb_val::size () const;
template <>
mdb_val::db_val (size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();
}
