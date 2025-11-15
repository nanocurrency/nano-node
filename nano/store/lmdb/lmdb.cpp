#include <nano/lib/block_sideband.hpp>
#include <nano/lib/block_type.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/utility.hpp>
#include <nano/store/lmdb/wallet_value.hpp>
#include <nano/store/typed_iterator_templ.hpp>
#include <nano/store/version.hpp>
#include <nano/store/versioning.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstring>
#include <queue>
#include <stdexcept>

template class nano::store::typed_iterator<nano::account, nano::account_info_v22>;

nano::store::lmdb::component::component (nano::logger & logger_a, std::filesystem::path const & path_a, nano::ledger_constants & constants, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, nano::lmdb_config const & lmdb_config_a, bool backup_before_upgrade_a, nano::store::open_mode mode_a) :
	// clang-format off
	nano::store::component{
		block_store,
		account_store,
		pending_store,
		online_weight_store,
		pruned_store,
		peer_store,
		confirmation_height_store,
		final_vote_store,
		version_store,
		rep_weight_store
	},
	// clang-format on
	block_store{ *this },
	account_store{ *this },
	pending_store{ *this },
	online_weight_store{ *this },
	pruned_store{ *this },
	peer_store{ *this },
	confirmation_height_store{ *this },
	final_vote_store{ *this },
	version_store{ *this },
	rep_weight_store{ *this },
	database_path{ path_a },
	mode{ mode_a },
	logger{ logger_a },
	env (path_a, nano::store::lmdb::env::options::make ().set_config (lmdb_config_a).set_use_no_mem_init (true).set_read_only (mode_a == nano::store::open_mode::read_only)),
	mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
	txn_tracking_enabled (txn_tracking_config_a.enable)
{
	logger.info (nano::log::type::lmdb, "Initializing ledger store: {}", database_path.string ());

	debug_assert (path_a.filename () == "data.ldb");

	auto is_fully_upgraded (false);
	auto is_fresh_db (false);
	{
		auto transaction (tx_begin_read ());
		auto err = mdb_dbi_open (env.tx (transaction), "meta", 0, &version_store.meta_handle);
		is_fresh_db = err != MDB_SUCCESS;
		if (err == MDB_SUCCESS)
		{
			is_fully_upgraded = (version.get (transaction) == version_current);
			mdb_dbi_close (env, version_store.meta_handle);
		}
	}

	// Only open a write lock when upgrades are needed. This is because CLI commands
	// open inactive nodes which can otherwise be locked here if there is a long write
	// (can be a few minutes with the --fast_bootstrap flag for instance)
	if (!is_fully_upgraded)
	{
		if (mode == nano::store::open_mode::read_only)
		{
			// Either following cases cannot run in read-only mode:
			// a) there is no database yet, the access needs to be in write mode for it to be created;
			// b) it will upgrade, and it is not possible to do it in read-only mode.
			throw std::runtime_error ("Database requires upgrade but was opened in read-only mode");
		}

		if (!is_fresh_db)
		{
			logger.info (nano::log::type::lmdb, "Upgrade in progress...");

			if (backup_before_upgrade_a)
			{
				create_backup_file (env, path_a, logger);
			}
		}
		auto needs_vacuuming = false;
		{
			auto transaction (tx_begin_write ());
			open_databases (transaction, MDB_CREATE);
			do_upgrades (transaction, constants, needs_vacuuming);
			logger.info (nano::log::type::lmdb, "Database upgraded successfully to version {}", version_current);
		}

		if (needs_vacuuming)
		{
			logger.info (nano::log::type::lmdb, "Ledger vacuum in progress...");

			auto vacuum_success = vacuum_after_upgrade (path_a, lmdb_config_a);
			if (vacuum_success)
			{
				logger.info (nano::log::type::lmdb, "Ledger vacuum completed");
			}
			else
			{
				logger.error (nano::log::type::lmdb, "Ledger vacuum failed");
				logger.error (nano::log::type::lmdb, "(Optional) Please ensure enough disk space is available for a copy of the database and try to vacuum after shutting down the node");
			}
		}
	}
	else
	{
		auto transaction (tx_begin_read ());
		open_databases (transaction, 0);
	}
}

bool nano::store::lmdb::component::vacuum_after_upgrade (std::filesystem::path const & path_a, nano::lmdb_config const & lmdb_config_a)
{
	// Vacuum the database. This is not a required step and may actually fail if there isn't enough storage space.
	auto vacuum_path = path_a.parent_path () / "vacuumed.ldb";

	logger.info (nano::log::type::lmdb, "Creating vacuumed ledger file: {}", vacuum_path.string ());

	auto vacuum_success = copy_db (vacuum_path);
	if (vacuum_success)
	{
		// Need to close the database to release the file handle
		mdb_env_sync (env, true);

		// Replace the ledger file with the vacuumed one
		std::filesystem::rename (vacuum_path, path_a);

		// Set up the environment again
		auto options = nano::store::lmdb::env::options::make ()
					   .set_config (lmdb_config_a)
					   .set_use_no_mem_init (true);
		env.init (path_a, options);
		auto transaction (tx_begin_read ());
		open_databases (transaction, 0);
	}
	else
	{
		// The vacuum file can be in an inconsistent state if there wasn't enough space to create it
		std::filesystem::remove (vacuum_path);
	}
	return vacuum_success;
}

void nano::store::lmdb::component::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

void nano::store::lmdb::component::serialize_memory_stats (boost::property_tree::ptree & json)
{
	MDB_stat stats;
	auto status (mdb_env_stat (env, &stats));
	release_assert (success (status), error_string (status));
	json.put ("branch_pages", stats.ms_branch_pages);
	json.put ("depth", stats.ms_depth);
	json.put ("entries", stats.ms_entries);
	json.put ("leaf_pages", stats.ms_leaf_pages);
	json.put ("overflow_pages", stats.ms_overflow_pages);
	json.put ("page_size", stats.ms_psize);
}

nano::store::write_transaction nano::store::lmdb::component::tx_begin_write ()
{
	return env.tx_begin_write (create_txn_callbacks ());
}

nano::store::read_transaction nano::store::lmdb::component::tx_begin_read () const
{
	return env.tx_begin_read (create_txn_callbacks ());
}

std::string nano::store::lmdb::component::vendor_get () const
{
	return boost::str (boost::format ("LMDB %1%.%2%.%3%") % MDB_VERSION_MAJOR % MDB_VERSION_MINOR % MDB_VERSION_PATCH);
}

std::filesystem::path nano::store::lmdb::component::get_database_path () const
{
	return database_path;
}

nano::store::open_mode nano::store::lmdb::component::get_mode () const
{
	return mode;
}

nano::store::lmdb::txn_callbacks nano::store::lmdb::component::create_txn_callbacks () const
{
	nano::store::lmdb::txn_callbacks mdb_txn_callbacks;
	if (txn_tracking_enabled)
	{
		mdb_txn_callbacks.txn_start = ([&mdb_txn_tracker = mdb_txn_tracker] (store::transaction_impl const * transaction_impl) {
			mdb_txn_tracker.add (transaction_impl);
		});
		mdb_txn_callbacks.txn_end = ([&mdb_txn_tracker = mdb_txn_tracker] (store::transaction_impl const * transaction_impl) {
			mdb_txn_tracker.erase (transaction_impl);
		});
	}
	return mdb_txn_callbacks;
}

void nano::store::lmdb::component::open_table (store::transaction const & transaction_a, char const * name, unsigned flags, MDB_dbi & handle)
{
	auto status = mdb_dbi_open (env.tx (transaction_a), name, flags, &handle);
	if (status != 0)
	{
		throw std::runtime_error ("Failed to open " + std::string (name) + " database: " + error_string (status));
	}
}

void nano::store::lmdb::component::open_databases (store::transaction const & transaction_a, unsigned flags)
{
	open_table (transaction_a, "online_weight", flags, online_weight_store.online_weight_handle);
	open_table (transaction_a, "meta", flags, version_store.meta_handle);
	open_table (transaction_a, "peers", flags, peer_store.peers_handle);
	open_table (transaction_a, "pruned", flags, pruned_store.pruned_handle);
	open_table (transaction_a, "confirmation_height", flags, confirmation_height_store.confirmation_height_handle);
	open_table (transaction_a, "accounts", flags, account_store.accounts_v0_handle);
	account_store.accounts_handle = account_store.accounts_v0_handle;
	open_table (transaction_a, "pending", flags, pending_store.pending_v0_handle);
	pending_store.pending_handle = pending_store.pending_v0_handle;
	open_table (transaction_a, "final_votes", flags, final_vote_store.final_votes_handle);
	open_table (transaction_a, "blocks", MDB_CREATE, block_store.blocks_handle);
	open_table (transaction_a, "successors", flags, block_store.successors_handle);
	open_table (transaction_a, "rep_weights", flags, rep_weight_store.rep_weights_handle);
}

void nano::store::lmdb::component::do_upgrades (store::write_transaction & transaction, nano::ledger_constants & constants, bool & needs_vacuuming)
{
	auto version_l = version.get (transaction);
	if (version_l < version_minimum)
	{
		logger.critical (nano::log::type::lmdb, "The version of the ledger ({}) is lower than the minimum ({}) which is supported for upgrades. Either upgrade a node first or delete the ledger.", version_l, version_minimum);
		throw std::runtime_error ("Ledger version " + std::to_string (version_l) + " is lower than minimum supported version " + std::to_string (version_minimum));
	}
	switch (version_l)
	{
		case 21:
			upgrade_v21_to_v22 (transaction);
			[[fallthrough]];
		case 22:
			upgrade_v22_to_v23 (transaction);
			[[fallthrough]];
		case 23:
			upgrade_v23_to_v24 (transaction);
			[[fallthrough]];
		case 24:
			break;
		default:
			logger.critical (nano::log::type::lmdb, "The version of the ledger ({}) is too high for this node", version_l);
			throw std::runtime_error ("Ledger version " + std::to_string (version_l) + " is too high for this node");
	}
}

void nano::store::lmdb::component::upgrade_v21_to_v22 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22...");

	MDB_dbi unchecked_handle{ 0 };
	auto status1 = mdb_dbi_open (env.tx (transaction), "unchecked", MDB_CREATE, &unchecked_handle);
	release_assert (success (status1), error_string (status1));

	auto status2 = mdb_drop (env.tx (transaction), unchecked_handle, 1); // del = 1, to delete it from the environment and close the DB handle.
	release_assert (success (status2), error_string (status2));

	version.put (transaction, 22);

	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void nano::store::lmdb::component::upgrade_v22_to_v23 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v22 to v23...");

	drop (transaction, tables::rep_weights);
	transaction.refresh ();

	release_assert (rep_weight.begin (tx_begin_read ()) == rep_weight.end (transaction), "rep weights table must be empty before upgrading to v23");

	auto iterate_accounts = [this] (auto && func) {
		auto transaction = tx_begin_read ();

		// Manually create v22 compatible iterator to read accounts
		auto it = typed_iterator<nano::account, nano::account_info_v22>{ store::iterator{ iterator::begin (env.tx (transaction), account_store.accounts_handle) } };
		auto const end = typed_iterator<nano::account, nano::account_info_v22>{ store::iterator{ iterator::end (env.tx (transaction), account_store.accounts_handle) } };

		for (; it != end; ++it)
		{
			auto const & account = it->first;
			auto const & account_info = it->second;

			func (account, account_info);
		}
	};

	// TODO: Make this smaller in dev builds
	const size_t batch_size = 250000;

	size_t processed = 0;
	iterate_accounts ([this, &transaction, &processed] (nano::account const & account, nano::account_info_v22 const & account_info) {
		if (!account_info.balance.is_zero ())
		{
			nano::uint128_t total{ 0 };
			nano::store::lmdb::db_val value;
			auto status = get (transaction, tables::rep_weights, account_info.representative, value);
			if (success (status))
			{
				total = nano::amount{ value }.number ();
			}
			total += account_info.balance.number ();
			status = put (transaction, tables::rep_weights, account_info.representative, nano::amount{ total });
			release_assert_success (status);
		}

		processed++;
		if (processed % batch_size == 0)
		{
			logger.info (nano::log::type::lmdb, "Processed {} accounts", processed);
			transaction.refresh (); // Refresh to prevent excessive memory usage
		}
	});

	logger.info (nano::log::type::lmdb, "Done processing {} accounts", processed);
	version.put (transaction, 23);

	logger.info (nano::log::type::lmdb, "Upgrading database from v22 to v23 completed");
}

void nano::store::lmdb::component::upgrade_v23_to_v24 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v23 to v24...");

	MDB_dbi frontiers_handle{ 0 };
	auto status1 = mdb_dbi_open (env.tx (transaction), "frontiers", MDB_CREATE, &frontiers_handle);
	release_assert (success (status1), error_string (status1));

	auto status2 = mdb_drop (env.tx (transaction), frontiers_handle, 1); // del = 1, to delete it from the environment and close the DB handle.
	release_assert (success (status2), error_string (status2));

	version.put (transaction, 24);

	logger.info (nano::log::type::lmdb, "Upgrading database from v23 to v24 completed");
}

/** Takes a filepath, appends '_backup_<timestamp>' to the end (but before any extension) and saves that file in the same directory */
void nano::store::lmdb::component::create_backup_file (nano::store::lmdb::env & env_a, std::filesystem::path const & filepath_a, nano::logger & logger)
{
	auto extension = filepath_a.extension ();
	auto filename_without_extension = filepath_a.filename ().replace_extension ("");
	auto orig_filepath = filepath_a;
	auto & backup_path = orig_filepath.remove_filename ();
	auto backup_filename = filename_without_extension;
	backup_filename += "_backup_";
	backup_filename += std::to_string (std::chrono::system_clock::now ().time_since_epoch ().count ());
	backup_filename += extension;
	auto backup_filepath = backup_path / backup_filename;

	logger.info (nano::log::type::lmdb, "Performing {} backup before database upgrade...", filepath_a.filename ().string ());

	auto error (mdb_env_copy (env_a, backup_filepath.string ().c_str ()));
	if (error)
	{
		logger.critical (nano::log::type::lmdb, "Database backup failed");
		std::exit (1);
	}
	else
	{
		logger.info (nano::log::type::lmdb, "Database backup completed. Backup can be found at: {}", backup_filepath.string ());
	}
}

bool nano::store::lmdb::component::exists (store::transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const
{
	nano::store::lmdb::db_val junk;
	auto status = get (transaction_a, table_a, key_a, junk);
	release_assert (success (status) || not_found (status), error_string (status));
	return (status == MDB_SUCCESS);
}

int nano::store::lmdb::component::get (store::transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val & value_a) const
{
	auto mdb_key = to_mdb_val (key_a);
	MDB_val mdb_value{};

	auto result = mdb_get (env.tx (transaction_a), table_to_dbi (table_a), &mdb_key, &mdb_value);
	if (result == MDB_SUCCESS)
	{
		value_a = from_mdb_val (mdb_value);
	}
	return result;
}

int nano::store::lmdb::component::put (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val const & value_a) const
{
	auto mdb_key = to_mdb_val (key_a);
	auto mdb_value = to_mdb_val (value_a);
	return (mdb_put (env.tx (transaction_a), table_to_dbi (table_a), &mdb_key, &mdb_value, 0));
}

int nano::store::lmdb::component::del (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const
{
	auto mdb_key = to_mdb_val (key_a);
	return (mdb_del (env.tx (transaction_a), table_to_dbi (table_a), &mdb_key, nullptr));
}

int nano::store::lmdb::component::drop (store::write_transaction const & transaction_a, tables table_a)
{
	return clear (transaction_a, table_to_dbi (table_a));
}

int nano::store::lmdb::component::clear (store::write_transaction const & transaction_a, MDB_dbi handle_a)
{
	return mdb_drop (env.tx (transaction_a), handle_a, 0);
}

uint64_t nano::store::lmdb::component::count (store::transaction const & transaction_a, tables table_a) const
{
	return count (transaction_a, table_to_dbi (table_a));
}

uint64_t nano::store::lmdb::component::count (store::transaction const & transaction_a, MDB_dbi db_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), db_a, &stats));
	release_assert_success (status);
	return (stats.ms_entries);
}

MDB_dbi nano::store::lmdb::component::table_to_dbi (tables table_a) const
{
	switch (table_a)
	{
		case tables::accounts:
			return account_store.accounts_handle;
		case tables::blocks:
			return block_store.blocks_handle;
		case tables::pending:
			return pending_store.pending_handle;
		case tables::online_weight:
			return online_weight_store.online_weight_handle;
		case tables::meta:
			return version_store.meta_handle;
		case tables::peers:
			return peer_store.peers_handle;
		case tables::pruned:
			return pruned_store.pruned_handle;
		case tables::confirmation_height:
			return confirmation_height_store.confirmation_height_handle;
		case tables::final_votes:
			return final_vote_store.final_votes_handle;
		case tables::successors:
			return block_store.successors_handle;
		case tables::rep_weights:
			return rep_weight_store.rep_weights_handle;
		default:
			release_assert (false);
			return peer_store.peers_handle;
	}
}

bool nano::store::lmdb::component::not_found (int status) const
{
	return nano::store::lmdb::not_found (status);
}

bool nano::store::lmdb::component::success (int status) const
{
	return nano::store::lmdb::success (status);
}

std::string nano::store::lmdb::component::error_string (int status) const
{
	return nano::store::lmdb::error_string (status);
}

bool nano::store::lmdb::component::copy_db (std::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (env, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void nano::store::lmdb::component::rebuild_db (store::write_transaction const & transaction_a)
{
	// Tables with uint256_union key
	std::vector<MDB_dbi> tables = { account_store.accounts_handle, block_store.blocks_handle, pruned_store.pruned_handle, confirmation_height_store.confirmation_height_handle };
	for (auto const & table : tables)
	{
		MDB_dbi temp;
		mdb_dbi_open (env.tx (transaction_a), "temp_table", MDB_CREATE, &temp);
		// Copy all values to temporary table
		for (typed_iterator<nano::uint256_union, nano::store::lmdb::db_val> i{ store::iterator{ iterator::begin (env.tx (transaction_a), table) } }, n{ store::iterator{ iterator::end (env.tx (transaction_a), table) } }; i != n; ++i)
		{
			nano::store::lmdb::db_val key_val (i->first);
			auto mdb_key = to_mdb_val (key_val);
			auto mdb_value = to_mdb_val (i->second);
			auto s = mdb_put (env.tx (transaction_a), temp, &mdb_key, &mdb_value, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, table) == count (transaction_a, temp));
		// Clear existing table
		mdb_drop (env.tx (transaction_a), table, 0);
		// Put values from copy
		for (typed_iterator<nano::uint256_union, nano::store::lmdb::db_val> i{ store::iterator{ iterator::begin (env.tx (transaction_a), temp) } }, n{ store::iterator{ iterator::end (env.tx (transaction_a), temp) } }; i != n; ++i)
		{
			nano::store::lmdb::db_val key_val (i->first);
			auto mdb_key = to_mdb_val (key_val);
			auto mdb_value = to_mdb_val (i->second);
			auto s = mdb_put (env.tx (transaction_a), table, &mdb_key, &mdb_value, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, table) == count (transaction_a, temp));
		// Remove temporary table
		mdb_drop (env.tx (transaction_a), temp, 1);
	}
	// Pending table
	{
		MDB_dbi temp;
		mdb_dbi_open (env.tx (transaction_a), "temp_table", MDB_CREATE, &temp);
		// Copy all values to temporary table
		for (typed_iterator<nano::pending_key, nano::pending_info> i{ store::iterator{ iterator::begin (env.tx (transaction_a), pending_store.pending_handle) } }, n{ store::iterator{ iterator::end (env.tx (transaction_a), pending_store.pending_handle) } }; i != n; ++i)
		{
			nano::store::lmdb::db_val key_val (i->first);
			nano::store::lmdb::db_val value_val (i->second);
			auto mdb_key = to_mdb_val (key_val);
			auto mdb_value = to_mdb_val (value_val);
			auto s = mdb_put (env.tx (transaction_a), temp, &mdb_key, &mdb_value, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		mdb_drop (env.tx (transaction_a), pending_store.pending_handle, 0);
		// Put values from copy
		for (typed_iterator<nano::pending_key, nano::pending_info> i{ store::iterator{ iterator::begin (env.tx (transaction_a), temp) } }, n{ store::iterator{ iterator::end (env.tx (transaction_a), temp) } }; i != n; ++i)
		{
			nano::store::lmdb::db_val key_val (i->first);
			nano::store::lmdb::db_val value_val (i->second);
			auto mdb_key = to_mdb_val (key_val);
			auto mdb_value = to_mdb_val (value_val);
			auto s = mdb_put (env.tx (transaction_a), pending_store.pending_handle, &mdb_key, &mdb_value, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		mdb_drop (env.tx (transaction_a), temp, 1);
	}
}

nano::store::lmdb::component::upgrade_counters::upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1) :
	before_v0 (count_before_v0),
	before_v1 (count_before_v1)
{
}

bool nano::store::lmdb::component::upgrade_counters::are_equal () const
{
	return (before_v0 == after_v0) && (before_v1 == after_v1);
}

unsigned nano::store::lmdb::component::max_block_write_batch_num () const
{
	return std::numeric_limits<unsigned>::max ();
}

/*
 *
 */

bool nano::store::lmdb::success (int status)
{
	return (MDB_SUCCESS == status);
}

bool nano::store::lmdb::not_found (int status)
{
	return (MDB_NOTFOUND == status);
}

std::string nano::store::lmdb::error_string (int status)
{
	return "status: " + std::to_string (status) + " (" + mdb_strerror (status) + ")";
}