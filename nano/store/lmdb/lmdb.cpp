#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/wallet_value.hpp>
#include <nano/store/version.hpp>
#include <nano/store/versioning.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <queue>

nano::store::lmdb::component::component (nano::logger & logger_a, std::filesystem::path const & path_a, nano::ledger_constants & constants, nano::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, nano::lmdb_config const & lmdb_config_a, bool backup_before_upgrade_a) :
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
		rep_weight_store,
		false // write_queue use_noops
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
	logger{ logger_a },
	env (error, path_a, nano::store::lmdb::env::options::make ().set_config (lmdb_config_a).set_use_no_mem_init (true)),
	mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
	txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error)
	{
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
				open_databases (error, transaction, MDB_CREATE);
				if (!error)
				{
					error |= do_upgrades (transaction, constants, needs_vacuuming);
				}
			}

			if (needs_vacuuming)
			{
				logger.info (nano::log::type::lmdb, "Ledger vaccum in progress...");

				auto vacuum_success = vacuum_after_upgrade (path_a, lmdb_config_a);
				if (vacuum_success)
				{
					logger.info (nano::log::type::lmdb, "Ledger vacuum completed");
				}
				else
				{
					logger.error (nano::log::type::lmdb, "Ledger vaccum failed");
					logger.error (nano::log::type::lmdb, "(Optional) Please ensure enough disk space is available for a copy of the database and try to vacuum after shutting down the node");
				}
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error, transaction, 0);
		}
	}
}

bool nano::store::lmdb::component::vacuum_after_upgrade (std::filesystem::path const & path_a, nano::lmdb_config const & lmdb_config_a)
{
	// Vacuum the database. This is not a required step and may actually fail if there isn't enough storage space.
	auto vacuum_path = path_a.parent_path () / "vacuumed.ldb";

	auto vacuum_success = copy_db (vacuum_path);
	if (vacuum_success)
	{
		// Need to close the database to release the file handle
		mdb_env_sync (env.environment, true);
		mdb_env_close (env.environment);
		env.environment = nullptr;

		// Replace the ledger file with the vacuumed one
		std::filesystem::rename (vacuum_path, path_a);

		// Set up the environment again
		auto options = nano::store::lmdb::env::options::make ()
					   .set_config (lmdb_config_a)
					   .set_use_no_mem_init (true);
		env.init (error, path_a, options);
		if (!error)
		{
			auto transaction (tx_begin_read ());
			open_databases (error, transaction, 0);
		}
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
	auto status (mdb_env_stat (env.environment, &stats));
	release_assert (status == 0);
	json.put ("branch_pages", stats.ms_branch_pages);
	json.put ("depth", stats.ms_depth);
	json.put ("entries", stats.ms_entries);
	json.put ("leaf_pages", stats.ms_leaf_pages);
	json.put ("overflow_pages", stats.ms_overflow_pages);
	json.put ("page_size", stats.ms_psize);
}

nano::store::write_transaction nano::store::lmdb::component::tx_begin_write (std::vector<nano::tables> const &, std::vector<nano::tables> const &)
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

void nano::store::lmdb::component::open_databases (bool & error_a, store::transaction const & transaction_a, unsigned flags)
{
	error_a |= mdb_dbi_open (env.tx (transaction_a), "online_weight", flags, &online_weight_store.online_weight_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "meta", flags, &version_store.meta_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "peers", flags, &peer_store.peers_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pruned", flags, &pruned_store.pruned_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "confirmation_height", flags, &confirmation_height_store.confirmation_height_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &account_store.accounts_v0_handle) != 0;
	account_store.accounts_handle = account_store.accounts_v0_handle;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_store.pending_v0_handle) != 0;
	pending_store.pending_handle = pending_store.pending_v0_handle;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "final_votes", flags, &final_vote_store.final_votes_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "blocks", MDB_CREATE, &block_store.blocks_handle) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "rep_weights", flags, &rep_weight_store.rep_weights_handle) != 0;
}

bool nano::store::lmdb::component::do_upgrades (store::write_transaction & transaction_a, nano::ledger_constants & constants, bool & needs_vacuuming)
{
	auto error (false);
	auto version_l = version.get (transaction_a);
	if (version_l < version_minimum)
	{
		logger.critical (nano::log::type::lmdb, "The version of the ledger ({}) is lower than the minimum ({}) which is supported for upgrades. Either upgrade a node first or delete the ledger.", version_l, version_minimum);
		return true;
	}
	switch (version_l)
	{
		case 21:
			upgrade_v21_to_v22 (transaction_a);
			[[fallthrough]];
		case 22:
			upgrade_v22_to_v23 (transaction_a);
			[[fallthrough]];
		case 23:
			upgrade_v23_to_v24 (transaction_a);
			[[fallthrough]];
		case 24:
			break;
		default:
			logger.critical (nano::log::type::lmdb, "The version of the ledger ({}) is too high for this node", version_l);
			error = true;
			break;
	}
	return error;
}

void nano::store::lmdb::component::upgrade_v21_to_v22 (store::write_transaction const & transaction_a)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22...");

	MDB_dbi unchecked_handle{ 0 };
	release_assert (!mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked_handle));
	release_assert (!mdb_drop (env.tx (transaction_a), unchecked_handle, 1)); // del = 1, to delete it from the environment and close the DB handle.
	version.put (transaction_a, 22);

	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void nano::store::lmdb::component::upgrade_v22_to_v23 (store::write_transaction const & transaction_a)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v22 to v23...");
	auto i{ make_iterator<nano::account, nano::account_info_v22> (transaction_a, tables::accounts) };
	auto end{ store::iterator<nano::account, nano::account_info_v22> (nullptr) };
	uint64_t processed_accounts = 0;
	for (; i != end; ++i)
	{
		if (!i->second.balance.is_zero ())
		{
			nano::uint128_t total{ 0 };
			nano::store::lmdb::db_val value;
			auto status = get (transaction_a, tables::rep_weights, i->second.representative, value);
			if (success (status))
			{
				total = nano::amount{ value }.number ();
			}
			total += i->second.balance.number ();
			status = put (transaction_a, tables::rep_weights, i->second.representative, nano::amount{ total });
			release_assert_success (status);
		}
		processed_accounts++;
		if (processed_accounts % 250000 == 0)
		{
			logger.info (nano::log::type::lmdb, "Processed {} accounts", processed_accounts);
		}
	}
	logger.info (nano::log::type::lmdb, "Processed {} accounts", processed_accounts);
	version.put (transaction_a, 23);
	logger.info (nano::log::type::lmdb, "Upgrading database from v22 to v23 completed");
}

void nano::store::lmdb::component::upgrade_v23_to_v24 (store::write_transaction const & transaction_a)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v23 to v24...");

	MDB_dbi frontiers_handle{ 0 };
	release_assert (!mdb_dbi_open (env.tx (transaction_a), "frontiers", MDB_CREATE, &frontiers_handle));
	release_assert (!mdb_drop (env.tx (transaction_a), frontiers_handle, 1)); // del = 1, to delete it from the environment and close the DB handle.
	version.put (transaction_a, 24);
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
	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	return (status == MDB_SUCCESS);
}

int nano::store::lmdb::component::get (store::transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val & value_a) const
{
	return mdb_get (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a);
}

int nano::store::lmdb::component::put (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val const & value_a) const
{
	return (mdb_put (env.tx (transaction_a), table_to_dbi (table_a), key_a, value_a, 0));
}

int nano::store::lmdb::component::del (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const
{
	return (mdb_del (env.tx (transaction_a), table_to_dbi (table_a), key_a, nullptr));
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
		case tables::rep_weights:
			return rep_weight_store.rep_weights_handle;
		default:
			release_assert (false);
			return peer_store.peers_handle;
	}
}

bool nano::store::lmdb::component::not_found (int status) const
{
	return (status_code_not_found () == status);
}

bool nano::store::lmdb::component::success (int status) const
{
	return (MDB_SUCCESS == status);
}

int nano::store::lmdb::component::status_code_not_found () const
{
	return MDB_NOTFOUND;
}

std::string nano::store::lmdb::component::error_string (int status) const
{
	return mdb_strerror (status);
}

bool nano::store::lmdb::component::copy_db (std::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
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
		for (auto i (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (std::make_unique<nano::store::lmdb::iterator<nano::uint256_union, nano::store::lmdb::db_val>> (transaction_a, env, table))), n (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (env.tx (transaction_a), temp, nano::store::lmdb::db_val (i->first), i->second, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, table) == count (transaction_a, temp));
		// Clear existing table
		mdb_drop (env.tx (transaction_a), table, 0);
		// Put values from copy
		for (auto i (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (std::make_unique<nano::store::lmdb::iterator<nano::uint256_union, nano::store::lmdb::db_val>> (transaction_a, env, temp))), n (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (env.tx (transaction_a), table, nano::store::lmdb::db_val (i->first), i->second, MDB_APPEND);
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
		for (auto i (store::iterator<nano::pending_key, nano::pending_info> (std::make_unique<nano::store::lmdb::iterator<nano::pending_key, nano::pending_info>> (transaction_a, env, pending_store.pending_handle))), n (store::iterator<nano::pending_key, nano::pending_info> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (env.tx (transaction_a), temp, nano::store::lmdb::db_val (i->first), nano::store::lmdb::db_val (i->second), MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		mdb_drop (env.tx (transaction_a), pending_store.pending_handle, 0);
		// Put values from copy
		for (auto i (store::iterator<nano::pending_key, nano::pending_info> (std::make_unique<nano::store::lmdb::iterator<nano::pending_key, nano::pending_info>> (transaction_a, env, temp))), n (store::iterator<nano::pending_key, nano::pending_info> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (env.tx (transaction_a), pending_store.pending_handle, nano::store::lmdb::db_val (i->first), nano::store::lmdb::db_val (i->second), MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		mdb_drop (env.tx (transaction_a), temp, 1);
	}
}

bool nano::store::lmdb::component::init_error () const
{
	return error;
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
