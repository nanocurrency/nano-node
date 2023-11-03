#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/rocksdb/account.hpp>
#include <nano/store/rocksdb/block.hpp>
#include <nano/store/rocksdb/confirmation_height.hpp>
#include <nano/store/rocksdb/final_vote.hpp>
#include <nano/store/rocksdb/frontier.hpp>
#include <nano/store/rocksdb/iterator.hpp>
#include <nano/store/rocksdb/online_weight.hpp>
#include <nano/store/rocksdb/peer.hpp>
#include <nano/store/rocksdb/pending.hpp>
#include <nano/store/rocksdb/pruned.hpp>
#include <nano/store/rocksdb/version.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>

namespace nano
{
class logging_mt;
class rocksdb_config;
class rocksdb_block_store_tombstone_count_Test;
}

namespace nano::store::rocksdb
{
class rocksdb_block_store_upgrade_v21_v22_Test;

/**
 	 * rocksdb implementation of the block store
 	 */
class component : public nano::store::component
{
private:
	nano::store::rocksdb::account account_store;
	nano::store::rocksdb::block block_store;
	nano::store::rocksdb::confirmation_height confirmation_height_store;
	nano::store::rocksdb::final_vote final_vote_store;
	nano::store::rocksdb::frontier frontier_store;
	nano::store::rocksdb::online_weight online_weight_store;
	nano::store::rocksdb::peer peer_store;
	nano::store::rocksdb::pending pending_store;
	nano::store::rocksdb::pruned pruned_store;
	nano::store::rocksdb::version version_store;

public:
	friend class nano::store::rocksdb::account;
	friend class nano::store::rocksdb::block;
	friend class nano::store::rocksdb::confirmation_height;
	friend class nano::store::rocksdb::final_vote;
	friend class nano::store::rocksdb::frontier;
	friend class nano::store::rocksdb::online_weight;
	friend class nano::store::rocksdb::peer;
	friend class nano::store::rocksdb::pending;
	friend class nano::store::rocksdb::pruned;
	friend class nano::store::rocksdb::version;

	explicit component (nano::logger_mt &, std::filesystem::path const &, nano::ledger_constants & constants, nano::rocksdb_config const & = nano::rocksdb_config{}, bool open_read_only = false);

	store::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_requiring_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) override;
	store::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	uint64_t count (store::transaction const & transaction_a, tables table_a) const override;

	bool exists (store::transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a) const;
	int get (store::transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val & value_a) const;
	int put (store::write_transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val const & value_a);
	int del (store::write_transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	bool copy_db (std::filesystem::path const & destination) override;
	void rebuild_db (store::write_transaction const & transaction_a) override;

	unsigned max_block_write_batch_num () const override;

	template <typename Key, typename Value>
	store::iterator<Key, Value> make_iterator (store::transaction const & transaction_a, tables table_a, bool const direction_asc = true) const
	{
		return store::iterator<Key, Value> (std::make_unique<nano::store::rocksdb::iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), nullptr, direction_asc));
	}

	template <typename Key, typename Value>
	store::iterator<Key, Value> make_iterator (store::transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key) const
	{
		return store::iterator<Key, Value> (std::make_unique<nano::store::rocksdb::iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), &key, true));
	}

	bool init_error () const override;

	std::string error_string (int status) const override;

private:
	bool error{ false };
	nano::logger_mt & logger;
	nano::ledger_constants & constants;
	// Optimistic transactions are used in write mode
	::rocksdb::OptimisticTransactionDB * optimistic_db = nullptr;
	std::unique_ptr<::rocksdb::DB> db;
	std::vector<std::unique_ptr<::rocksdb::ColumnFamilyHandle>> handles;
	std::shared_ptr<::rocksdb::TableFactory> small_table_factory;
	std::unordered_map<nano::tables, nano::mutex> write_lock_mutexes;
	nano::rocksdb_config rocksdb_config;
	unsigned const max_block_write_batch_num_m;

	class tombstone_info
	{
	public:
		tombstone_info (uint64_t, uint64_t const);
		std::atomic<uint64_t> num_since_last_flush;
		uint64_t const max;
	};

	std::unordered_map<nano::tables, tombstone_info> tombstone_map;
	std::unordered_map<char const *, nano::tables> cf_name_table_map;

	::rocksdb::Transaction * tx (store::transaction const & transaction_a) const;
	std::vector<nano::tables> all_tables () const;

	bool not_found (int status) const override;
	bool success (int status) const override;
	void release_assert_success (int const status) const
	{
		if (!success (status))
		{
			release_assert (false, error_string (status));
		}
	}
	int status_code_not_found () const override;
	int drop (store::write_transaction const &, tables) override;

	std::vector<::rocksdb::ColumnFamilyDescriptor> get_single_column_family (std::string cf_name) const;
	std::vector<::rocksdb::ColumnFamilyDescriptor> get_current_column_families (std::string const & path_a, ::rocksdb::Options const & options_a) const;
	::rocksdb::ColumnFamilyHandle * get_column_family (char const * name) const;
	bool column_family_exists (char const * name) const;
	::rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (::rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, std::filesystem::path const & path_a, bool open_read_only_a, ::rocksdb::Options const & options_a, std::vector<::rocksdb::ColumnFamilyDescriptor> column_families);

	bool do_upgrades (store::write_transaction const &);
	void upgrade_v21_to_v22 (store::write_transaction const &);

	void construct_column_family_mutexes ();
	::rocksdb::Options get_db_options ();
	::rocksdb::ColumnFamilyOptions get_common_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	::rocksdb::ColumnFamilyOptions get_active_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	::rocksdb::ColumnFamilyOptions get_small_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a) const;
	::rocksdb::BlockBasedTableOptions get_active_table_options (std::size_t lru_size) const;
	::rocksdb::BlockBasedTableOptions get_small_table_options () const;
	::rocksdb::ColumnFamilyOptions get_cf_options (std::string const & cf_name_a) const;

	void on_flush (::rocksdb::FlushJobInfo const &);
	void flush_table (nano::tables table_a);
	void flush_tombstones_check (nano::tables table_a);
	void generate_tombstone_map ();
	std::unordered_map<char const *, nano::tables> create_cf_name_table_map () const;

	std::vector<::rocksdb::ColumnFamilyDescriptor> create_column_families ();
	unsigned long long base_memtable_size_bytes () const;
	unsigned long long blocks_memtable_size_bytes () const;

	constexpr static int base_memtable_size = 16;
	constexpr static int base_block_cache_size = 8;

	friend class nano::rocksdb_block_store_tombstone_count_Test;
	friend class rocksdb_block_store_upgrade_v21_v22_Test;
};
} // namespace nano::store::rocksdb
