#pragma once

#include <nano/lib/lmdbconfig.hpp>
#include <nano/lib/logging.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/lmdb/lmdb_env.hpp>
#include <nano/store/lmdb/transaction_lmdb.hpp>

#include <unordered_map>

namespace nano::store::lmdb
{
/**
 * LMDB implementation of the backend interface.
 * Provides low-level database operations using LMDB.
 */
class backend_lmdb : public nano::store::backend
{
public:
	backend_lmdb (std::filesystem::path const & path, nano::lmdb_config const & config, nano::logger & logger, nano::store::txn_tracking_config const & txn_tracking_config = {});
	~backend_lmdb () override;

	int get (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val & value) const override;
	int put (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val const & value) override;
	int del (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key) override;
	bool exists (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const override;

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
	std::filesystem::path const database_path;
	nano::lmdb_config config;

	std::unique_ptr<nano::store::lmdb::env> env;
	std::unordered_map<nano::store::table, nano::store::lmdb::env::table_handle> table_handles;

	nano::store::lmdb::env::table_handle table_to_dbi (nano::store::table) const;
	void open_table (MDB_txn *, nano::store::table, std::string const & name, unsigned flags);
};
}
