#include <nano/store/lmdb/backend_lmdb.hpp>
#include <nano/store/lmdb/common.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/utility.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
backend_lmdb::backend_lmdb (std::filesystem::path const & path, nano::lmdb_config const & config_a, nano::logger & logger_a, nano::store::txn_tracking_config const & txn_tracking_config_a) :
	backend{ logger_a, txn_tracking_config_a },
	database_path{ path },
	config{ config_a }
{
}

backend_lmdb::~backend_lmdb ()
{
	close ();
}

void backend_lmdb::close_impl ()
{
	// Close all table handles
	if (env)
	{
		for (auto const & [table, dbi] : table_handles)
		{
			mdb_dbi_close (*env, dbi);
		}
		table_handles.clear ();
	}

	// Release environment
	env.reset ();
}

void backend_lmdb::open_impl (column_schema schema, nano::store::open_mode mode)
{
	release_assert (!env, "environment already open");
	release_assert (table_handles.empty ());

	// Create environment with appropriate options
	auto options = nano::store::lmdb::env::options::make ()
				   .set_config (config)
				   .set_use_no_mem_init (true)
				   .set_read_only (mode == nano::store::open_mode::read_only);

	env = std::make_unique<nano::store::lmdb::env> (database_path, options);

	bool const read_only = (mode == nano::store::open_mode::read_only);

	MDB_txn * mdb_txn{ nullptr };
	auto status = mdb_txn_begin (*env, nullptr, read_only ? MDB_RDONLY : 0, &mdb_txn);
	release_assert (success (status), error_string (status));

	for (auto const & [table, name] : schema)
	{
		open_table (mdb_txn, table, name, read_only ? 0 : MDB_CREATE);
	}

	status = mdb_txn_commit (mdb_txn);
	release_assert (success (status), "failed to commit lmdb opening transaction", error_string (status));
}

void backend_lmdb::open_table (MDB_txn * mdb_txn, nano::store::table table, std::string const & name, unsigned flags)
{
	MDB_dbi handle{};
	auto status = mdb_dbi_open (mdb_txn, name.c_str (), flags, &handle);
	if (!success (status))
	{
		throw std::runtime_error ("Failed to open " + std::string (name) + " database: " + error_string (status));
	}
	table_handles[table] = handle;
}

auto backend_lmdb::table_to_dbi (nano::store::table table) const -> nano::store::lmdb::env::table_handle
{
	auto it = table_handles.find (table);
	release_assert (it != table_handles.end (), "table not found");
	return it->second;
}

int backend_lmdb::get (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val & value) const
{
	auto mdb_key = to_mdb_val (key);
	MDB_val mdb_value{};

	auto result = mdb_get (env->tx (txn), table_to_dbi (table), &mdb_key, &mdb_value);
	if (result == MDB_SUCCESS)
	{
		value = from_mdb_val (mdb_value);
	}
	return result;
}

int backend_lmdb::put (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val const & value)
{
	auto mdb_key = to_mdb_val (key);
	auto mdb_value = to_mdb_val (value);
	return mdb_put (env->tx (txn), table_to_dbi (table), &mdb_key, &mdb_value, 0);
}

int backend_lmdb::del (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key)
{
	auto mdb_key = to_mdb_val (key);
	auto status = mdb_del (env->tx (txn), table_to_dbi (table), &mdb_key, nullptr);
	// Treat not_found as success for delete operations (key already absent)
	if (status == MDB_NOTFOUND)
	{
		return MDB_SUCCESS;
	}
	return status;
}

bool backend_lmdb::exists (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	nano::store::db_val junk;
	auto status = get (txn, table, key, junk);
	release_assert (success (status) || not_found (status), error_string (status));
	return success (status);
}

uint64_t backend_lmdb::count (nano::store::transaction const & txn, nano::store::table table) const
{
	MDB_stat stats;
	auto status = mdb_stat (env->tx (txn), table_to_dbi (table), &stats);
	release_assert (success (status), error_string (status));
	return stats.ms_entries;
}

bool backend_lmdb::count_is_exact (nano::store::table table) const
{
	// LMDB provides exact counts for all tables via mdb_stat
	return true;
}

int backend_lmdb::clear (nano::store::table table)
{
	auto txn = tx_begin_write ();
	auto status = mdb_drop (env->tx (txn), table_to_dbi (table), /* only empty the db */ 0);
	return status;
}

bool backend_lmdb::drop_table (std::string const & name)
{
	auto txn = tx_begin_write ();

	MDB_dbi dbi;
	auto status1 = mdb_dbi_open (env->tx (txn), name.c_str (), 0, &dbi);
	if (not_found (status1))
	{
		return false; // Table doesn't exist
	}
	release_assert (success (status1), error_string (status1));

	auto status2 = mdb_drop (env->tx (txn), dbi, /* delete from database */ 1);
	release_assert (success (status2), error_string (status2));

	// Remove from table_handles if it was tracked
	std::erase_if (table_handles, [dbi] (auto const & pair) {
		return pair.second == dbi;
	});

	return true;
}

bool backend_lmdb::table_exists (std::string const & name) const
{
	auto txn = tx_begin_read ();

	MDB_dbi dbi;
	auto status = mdb_dbi_open (env->tx (txn), name.c_str (), 0, &dbi);
	return success (status);
}

nano::store::iterator backend_lmdb::begin (nano::store::transaction const & txn, nano::store::table table) const
{
	return nano::store::iterator{ iterator::begin (env->tx (txn), table_to_dbi (table)) };
}

nano::store::iterator backend_lmdb::begin (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	auto mdb_key = to_mdb_val (key);
	return nano::store::iterator{ iterator::lower_bound (env->tx (txn), table_to_dbi (table), mdb_key) };
}

nano::store::iterator backend_lmdb::end (nano::store::transaction const & txn, nano::store::table table) const
{
	return nano::store::iterator{ iterator::end (env->tx (txn), table_to_dbi (table)) };
}

bool backend_lmdb::success (int status) const
{
	return nano::store::lmdb::success (status);
}

bool backend_lmdb::not_found (int status) const
{
	return nano::store::lmdb::not_found (status);
}

std::string backend_lmdb::error_string (int status) const
{
	return nano::store::lmdb::error_string (status);
}

nano::store::read_transaction backend_lmdb::tx_begin_read () const
{
	return env->tx_begin_read (txn_tracking_callbacks ());
}

nano::store::write_transaction backend_lmdb::tx_begin_write ()
{
	return env->tx_begin_write (txn_tracking_callbacks ());
}

void backend_lmdb::backup ()
{
	release_assert (env != nullptr, "database must be open to perform backup");

	auto extension = database_path.extension ();
	auto filename_without_extension = database_path.filename ().replace_extension ("");
	auto backup_path = database_path.parent_path ();
	auto backup_filename = filename_without_extension;
	backup_filename += "_backup_";
	backup_filename += std::to_string (std::chrono::system_clock::now ().time_since_epoch ().count ());
	backup_filename += extension;
	auto backup_filepath = backup_path / backup_filename;

	auto error = mdb_env_copy (*env, backup_filepath.string ().c_str ());
	if (error)
	{
		throw std::runtime_error ("Database backup failed: " + error_string (error));
	}
}

void backend_lmdb::copy_with_compaction (std::filesystem::path const & destination)
{
	auto status = mdb_env_copy2 (*env, destination.string ().c_str (), MDB_CP_COMPACT);
	if (status != MDB_SUCCESS)
	{
		throw std::runtime_error ("Database copy with compaction failed: " + error_string (status));
	}
}

std::string backend_lmdb::vendor_get () const
{
	return boost::str (boost::format ("LMDB %1%.%2%.%3%") % MDB_VERSION_MAJOR % MDB_VERSION_MINOR % MDB_VERSION_PATCH);
}

std::string backend_lmdb::get_database_path () const
{
	return database_path.string ();
}

void backend_lmdb::collect_memory_stats (boost::property_tree::ptree & ptree) const
{
	release_assert (env, "database must be open to collect memory stats");

	MDB_stat stats;
	auto status = mdb_env_stat (*env, &stats);
	release_assert (success (status), error_string (status));
	ptree.put ("branch_pages", stats.ms_branch_pages);
	ptree.put ("depth", stats.ms_depth);
	ptree.put ("entries", stats.ms_entries);
	ptree.put ("leaf_pages", stats.ms_leaf_pages);
	ptree.put ("overflow_pages", stats.ms_overflow_pages);
	ptree.put ("page_size", stats.ms_psize);
}
/*
 *
 */

bool success (int status)
{
	return MDB_SUCCESS == status;
}

bool not_found (int status)
{
	return MDB_NOTFOUND == status;
}

std::string error_string (int status)
{
	return "status: " + std::to_string (status) + " (" + mdb_strerror (status) + ")";
}
}
