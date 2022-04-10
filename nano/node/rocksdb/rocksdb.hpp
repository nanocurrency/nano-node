#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/rocksdb/account_store.hpp>
#include <nano/node/rocksdb/block_store.hpp>
#include <nano/node/rocksdb/confirmation_height_store.hpp>
#include <nano/node/rocksdb/final_vote_store.hpp>
#include <nano/node/rocksdb/frontier_store.hpp>
#include <nano/node/rocksdb/online_weight_store.hpp>
#include <nano/node/rocksdb/peer_store.hpp>
#include <nano/node/rocksdb/pending_store.hpp>
#include <nano/node/rocksdb/pruned_store.hpp>
#include <nano/node/rocksdb/rocksdb_iterator.hpp>
#include <nano/node/rocksdb/unchecked_store.hpp>
#include <nano/node/rocksdb/version_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/store_partial.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace nano
{
class logging_mt;
class rocksdb_config;
class rocksdb_store;

/**
 * rocksdb implementation of the block store
 */
class rocksdb_store : public store_partial<::rocksdb::Slice, rocksdb_store>
{
private:
	nano::rocksdb::account_store account_store;
	nano::rocksdb::block_store block_store;
	nano::rocksdb::confirmation_height_store confirmation_height_store;
	nano::rocksdb::final_vote_store final_vote_store;
	nano::rocksdb::frontier_store frontier_store;
	nano::rocksdb::online_weight_store online_weight_store;
	nano::rocksdb::peer_store peer_store;
	nano::rocksdb::pending_store pending_store;
	nano::rocksdb::pruned_store pruned_store;
	nano::rocksdb::unchecked_store unchecked_store;
	nano::rocksdb::version_store version_store;

public:
	friend class nano::rocksdb::account_store;
	friend class nano::rocksdb::block_store;
	friend class nano::rocksdb::confirmation_height_store;
	friend class nano::rocksdb::final_vote_store;
	friend class nano::rocksdb::frontier_store;
	friend class nano::rocksdb::online_weight_store;
	friend class nano::rocksdb::peer_store;
	friend class nano::rocksdb::pending_store;
	friend class nano::rocksdb::pruned_store;
	friend class nano::rocksdb::unchecked_store;
	friend class nano::rocksdb::version_store;

	explicit rocksdb_store (nano::logger_mt &, boost::filesystem::path const &, nano::ledger_constants & constants, nano::rocksdb_config const & = nano::rocksdb_config{}, bool open_read_only = false);

	nano::write_transaction tx_begin_write (std::vector<nano::tables> const & tables_requiring_lock = {}, std::vector<nano::tables> const & tables_no_lock = {}) override;
	nano::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	uint64_t count (nano::transaction const & transaction_a, tables table_a) const override;

	bool exists (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a) const;
	int get (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val & value_a) const;
	int put (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val const & value_a);
	int del (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	bool copy_db (boost::filesystem::path const & destination) override;
	void rebuild_db (nano::write_transaction const & transaction_a) override;

	unsigned max_block_write_batch_num () const override;

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, bool const direction_asc = true) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), nullptr, direction_asc));
	}

	template <typename Key, typename Value>
	nano::store_iterator<Key, Value> make_iterator (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key) const
	{
		return nano::store_iterator<Key, Value> (std::make_unique<nano::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), &key, true));
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

	::rocksdb::Transaction * tx (nano::transaction const & transaction_a) const;
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
	int drop (nano::write_transaction const &, tables) override;

	::rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (::rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a);

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

	friend class rocksdb_block_store_tombstone_count_Test;
};

extern template class store_partial<::rocksdb::Slice, rocksdb_store>;
}
