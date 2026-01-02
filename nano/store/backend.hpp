#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/result.hpp>
#include <nano/store/common.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/meta.hpp>
#include <nano/store/tables.hpp>
#include <nano/store/transaction.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace nano
{
enum class error_backend
{
	generic = 1,
	db_not_found,
	table_not_found,
	failure,
};
}
REGISTER_ERROR_CODES (nano, error_backend)

namespace nano::store
{
using backend_version_t = uint64_t;

struct backend_meta
{
	backend_version_t version;
};

using column_definition = std::pair<nano::store::table, std::string>;
using column_schema = std::set<column_definition>;

struct copy_progress
{
	size_t current_table_index;
	size_t total_tables;
	nano::store::table table;
	std::string table_name;
	uint64_t entries_copied;
	uint64_t total_entries;
};

using copy_progress_callback = std::function<void (copy_progress const &)>;

/**
 * Polymorphic backend interface for key-value database operations.
 * This provides the minimal interface that database backends (LMDB, RocksDB) must implement.
 * All business logic should be in common implementation classes that use this interface.
 */
class backend
{
public:
	virtual ~backend ();

	std::optional<backend_meta> fetch_meta ();

	void open (column_schema, nano::store::open_mode mode);
	void create (column_schema, nano::store::backend_version_t version);
	void close ();

	// Basic CRUD operations
	virtual int get (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val & value) const = 0;
	virtual int put (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key, nano::store::db_val const & value) = 0;
	virtual int del (nano::store::write_transaction const &, nano::store::table, nano::store::db_val const & key) = 0;
	virtual bool exists (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const = 0;

	// Table operations
	bool empty (nano::store::transaction const &) const; // Checks if all tables are empty
	bool empty (nano::store::transaction const &, nano::store::table) const;
	virtual uint64_t count (nano::store::transaction const &, nano::store::table) const = 0;
	virtual bool count_is_exact (nano::store::table) const = 0; // Returns true if count() returns exact value, false if estimate
	uint64_t count_exact (nano::store::transaction const &, nano::store::table) const; // Exact count via iteration (always accurate)
	virtual int clear (nano::store::table) = 0; // Empties the table but keeps it
	virtual bool drop_table (std::string const & name) = 0; // Deletes the table entirely
	virtual bool table_exists (std::string const & name) const = 0;

	// Iterator operations
	virtual nano::store::iterator begin (nano::store::transaction const &, nano::store::table) const = 0;
	virtual nano::store::iterator begin (nano::store::transaction const &, nano::store::table, nano::store::db_val const & key) const = 0;
	virtual nano::store::iterator end (nano::store::transaction const &, nano::store::table) const = 0;

	// Parallel iteration
	void for_each_par (nano::store::table,
	std::function<void (read_transaction const &, iterator, iterator)> const & action) const;

	// Copy all tables to another backend
	void copy_to (backend & destination,
	copy_progress_callback progress_callback = nullptr,
	size_t batch_size = 10000) const;

	virtual void copy_with_compaction (std::filesystem::path const & destination) = 0;
	virtual void backup () = 0;

	// Diagnostics (optional, backend-specific)
	virtual void collect_txn_tracker (boost::property_tree::ptree &, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time) const;
	virtual void collect_memory_stats (boost::property_tree::ptree &) const;

	// Status checking
	virtual bool success (int status) const = 0;
	virtual bool not_found (int status) const = 0;
	virtual std::string error_string (int status) const = 0;

	// Transaction management
	virtual nano::store::read_transaction tx_begin_read () const = 0;
	virtual nano::store::write_transaction tx_begin_write () = 0;

	// Helper methods
	void release_assert_success (int status) const;

	virtual std::string vendor_get () const = 0;
	virtual std::string get_database_path () const = 0;

	std::optional<nano::store::open_mode> get_mode () const;
	column_schema get_schema () const;
	backend_meta get_meta () const;

	nano::store::backend_version_t get_version (nano::store::transaction const &) const;
	void set_version (nano::store::write_transaction const &, nano::store::backend_version_t version);

protected:
	virtual void open_impl (column_schema, nano::store::open_mode) = 0;
	virtual void close_impl () = 0;

private:
	void load_meta ();

private:
	bool is_open{ false };
	nano::store::open_mode current_mode{};
	std::optional<backend_meta> current_meta{};
	column_schema current_schema{};

	nano::store::meta_view meta{ *this };

public:
	static nano::store::column_schema const schema_meta;
};

inline void backend::release_assert_success (int status) const
{
	if (!success (status))
	{
		release_assert (false, error_string (status));
	}
}
}