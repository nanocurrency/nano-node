#pragma once

#include <nano/lib/rocksdbconfig.hpp>
#include <nano/store/backend.hpp>

#include <functional>
#include <map>
#include <unordered_map>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/transaction_db.h>

namespace nano::store::rocksdb
{
/**
 * RocksDB implementation of the backend interface.
 */
class backend_rocksdb : public nano::store::backend
{
public:
	backend_rocksdb (std::filesystem::path const & path, nano::rocksdb_config const & config);
	~backend_rocksdb () override;

	int get (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val & value) const override;
	int put (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val const & value) override;
	int del (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key) override;
	bool exists (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const override;

	// WARNING: count() may return estimates for some tables
	// Use count_is_exact() to check, or use empty() for reliable emptiness checks
	uint64_t count (nano::store::transaction const &, nano::store::table) const override;
	bool count_is_exact (nano::store::table) const override;
	int clear (nano::store::table) override;
	bool drop_table (std::string const & name) override;
	bool table_exists (std::string const & name) const override;

	nano::store::iterator begin (nano::store::transaction const &, nano::store::table) const override;
	nano::store::iterator begin (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const override;
	nano::store::iterator end (nano::store::transaction const &, nano::store::table) const override;

	bool success (int status) const override;
	bool not_found (int status) const override;
	std::string error_string (int status) const override;

	nano::store::read_transaction tx_begin_read () const override;
	nano::store::write_transaction tx_begin_write () override;

	void copy_with_compaction (std::filesystem::path const & destination) override;
	void backup () override;

	void collect_memory_stats (boost::property_tree::ptree &) const override;

	std::string vendor_get () const override;
	std::string get_database_path () const override;

protected:
	void open_impl (column_schema schema, nano::store::open_mode mode) override;
	void close_impl () override;

private:
	void open_db (std::filesystem::path const & path, nano::store::open_mode mode, ::rocksdb::Options const & options, std::vector<::rocksdb::ColumnFamilyDescriptor> column_families);

	::rocksdb::ColumnFamilyHandle * table_to_column_family (nano::store::table) const;
	::rocksdb::ColumnFamilyHandle * get_column_family (std::string const & name) const;
	bool column_family_exists (std::string const & name) const;

	::rocksdb::Options get_db_options (nano::store::open_mode mode);
	::rocksdb::BlockBasedTableOptions get_table_options () const;
	::rocksdb::ColumnFamilyOptions get_cf_options (std::string const & cf_name) const;

private:
	std::filesystem::path const database_path;
	nano::rocksdb_config const config;

	std::unique_ptr<::rocksdb::DB> db;
	::rocksdb::TransactionDB * transaction_db{ nullptr };
	std::vector<std::unique_ptr<::rocksdb::ColumnFamilyHandle>> handles;
	std::map<nano::store::table, ::rocksdb::ColumnFamilyHandle *> table_handles;
	std::map<std::string, nano::store::table> name_to_table;

public: // Tombstone management
	class tombstone_info
	{
	public:
		tombstone_info (uint64_t num, uint64_t max_a);
		std::atomic<uint64_t> num_since_last_flush;
		uint64_t const max;
	};
	std::unordered_map<nano::store::table, tombstone_info> const & get_tombstone_map () const
	{
		return tombstone_map;
	}

private:
	std::unordered_map<nano::store::table, tombstone_info> tombstone_map;

	void generate_tombstone_map ();
	void flush_tombstones_check (nano::store::table);
	void flush_table (nano::store::table);
	void on_flush (::rocksdb::FlushJobInfo const & flush_info);
};
}
