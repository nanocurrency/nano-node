#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/iterator.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/options.hpp>
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
	env (::lmdb::env::create ()),
	mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
	txn_tracking_enabled (txn_tracking_config_a.enable)
{
	auto options = nano::store::lmdb::options::make ().set_config (lmdb_config_a).set_use_no_mem_init (true);
	options.apply (env);
	debug_assert (path_a.filename () == "data.ldb");
	env.open (path_a.string ().c_str (), options.flags ());
	if (!error)
	{
		auto is_fully_upgraded = false;
		auto is_fresh_db = false;
		{
			auto transaction = tx_begin_read ();
			try
			{
				::lmdb::dbi_open (tx (transaction), "meta", 0, &version_store.meta_handle);
				is_fully_upgraded = (version.get (transaction) == version_current);
				::lmdb::dbi_close (env, version_store.meta_handle);
			}
			catch (::lmdb::not_found_error const &)
			{
				is_fresh_db = true;
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
				auto transaction = tx_begin_write ();
				open_databases (transaction, MDB_CREATE);
				error |= do_upgrades (transaction, constants, needs_vacuuming);
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
			auto transaction = tx_begin_read ();
			open_databases (transaction, 0);
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
		env.sync (true);
		env.close ();

		// Replace the ledger file with the vacuumed one
		std::filesystem::rename (vacuum_path, path_a);

		// Set up the environment again
		auto options = nano::store::lmdb::options::make ()
					   .set_config (lmdb_config_a)
					   .set_use_no_mem_init (true);
		env = ::lmdb::env::create ();
		options.apply (env);
		env.open (path_a.string ().c_str (), options.flags ());
		auto transaction = tx_begin_read ();
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
	::lmdb::env_stat (env.handle (), &stats);
	json.put ("branch_pages", stats.ms_branch_pages);
	json.put ("depth", stats.ms_depth);
	json.put ("entries", stats.ms_entries);
	json.put ("leaf_pages", stats.ms_leaf_pages);
	json.put ("overflow_pages", stats.ms_overflow_pages);
	json.put ("page_size", stats.ms_psize);
}

nano::store::write_transaction nano::store::lmdb::component::tx_begin_write (std::vector<nano::tables> const &, std::vector<nano::tables> const &)
{
	return store::write_transaction{ std::make_unique<nano::store::lmdb::write_transaction_impl> (env, create_txn_callbacks ()), store_id };
}

nano::store::read_transaction nano::store::lmdb::component::tx_begin_read () const
{
	return store::read_transaction{ std::make_unique<nano::store::lmdb::read_transaction_impl> (env, create_txn_callbacks ()), store_id };
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

void nano::store::lmdb::component::open_databases (store::transaction const & transaction_a, unsigned flags)
{
	::lmdb::dbi_open (tx (transaction_a), "online_weight", flags, &online_weight_store.online_weight_handle);
	::lmdb::dbi_open (tx (transaction_a), "meta", flags, &version_store.meta_handle);
	::lmdb::dbi_open (tx (transaction_a), "peers", flags, &peer_store.peers_handle);
	::lmdb::dbi_open (tx (transaction_a), "pruned", flags, &pruned_store.pruned_handle);
	::lmdb::dbi_open (tx (transaction_a), "confirmation_height", flags, &confirmation_height_store.confirmation_height_handle);
	::lmdb::dbi_open (tx (transaction_a), "accounts", flags, &account_store.accounts_handle);
	::lmdb::dbi_open (tx (transaction_a), "pending", flags, &pending_store.pending_handle);
	::lmdb::dbi_open (tx (transaction_a), "final_votes", flags, &final_vote_store.final_votes_handle);
	::lmdb::dbi_open (tx (transaction_a), "blocks", MDB_CREATE, &block_store.blocks_handle);
	::lmdb::dbi_open (tx (transaction_a), "rep_weights", flags, &rep_weight_store.rep_weights_handle);
}

bool nano::store::lmdb::component::do_upgrades (store::write_transaction & transaction, nano::ledger_constants & constants, bool & needs_vacuuming)
{
	auto error (false);
	auto version_l = version.get (transaction);
	if (version_l < version_minimum)
	{
		logger.critical (nano::log::type::lmdb, "The version of the ledger ({}) is lower than the minimum ({}) which is supported for upgrades. Either upgrade a node first or delete the ledger.", version_l, version_minimum);
		return true;
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
			error = true;
			break;
	}
	return error;
}

void nano::store::lmdb::component::upgrade_v21_to_v22 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22...");

	MDB_dbi unchecked_handle{ 0 };
	::lmdb::dbi_open (tx (transaction), "unchecked", MDB_CREATE, &unchecked_handle);
	::lmdb::dbi_drop (tx (transaction), unchecked_handle, true);
	version.put (transaction, 22);

	logger.info (nano::log::type::lmdb, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void nano::store::lmdb::component::upgrade_v22_to_v23 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::lmdb, "Upgrading database from v22 to v23...");

	clear (transaction, table_to_dbi (tables::rep_weights));
	transaction.refresh ();

	release_assert (rep_weight.begin (tx_begin_read ()) == rep_weight.end (), "rep weights table must be empty before upgrading to v23");

	auto iterate_accounts = [this] (auto && func) {
		auto transaction = tx_begin_read ();

		// Manually create v22 compatible iterator to read accounts
		auto it = make_iterator<nano::account, nano::account_info_v22> (transaction, tables::accounts);
		auto const end = store::iterator<nano::account, nano::account_info_v22> (nullptr);

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
	::lmdb::dbi_open (tx (transaction), "frontiers", MDB_CREATE, &frontiers_handle);
	::lmdb::dbi_drop (tx (transaction), frontiers_handle, true);
	version.put (transaction, 24);
	logger.info (nano::log::type::lmdb, "Upgrading database from v23 to v24 completed");
}

/** Takes a filepath, appends '_backup_<timestamp>' to the end (but before any extension) and saves that file in the same directory */
void nano::store::lmdb::component::create_backup_file (::lmdb::env & env_a, std::filesystem::path const & filepath_a, nano::logger & logger)
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
	return mdb_get (tx (transaction_a), table_to_dbi (table_a), key_a, value_a);
}

int nano::store::lmdb::component::put (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a, nano::store::lmdb::db_val const & value_a) const
{
	return (mdb_put (tx (transaction_a), table_to_dbi (table_a), key_a, value_a, 0));
}

int nano::store::lmdb::component::del (store::write_transaction const & transaction_a, tables table_a, nano::store::lmdb::db_val const & key_a) const
{
	return (mdb_del (tx (transaction_a), table_to_dbi (table_a), key_a, nullptr));
}

int nano::store::lmdb::component::drop (store::write_transaction const & transaction_a, tables table_a)
{
	try
	{
		::lmdb::dbi_drop (tx (transaction_a), table_to_dbi (table_a), true);
		return 0;
	}
	catch (::lmdb::runtime_error const &)
	{
		return -1;
	}
}

void nano::store::lmdb::component::clear (store::write_transaction const & transaction_a, MDB_dbi handle_a)
{
	::lmdb::dbi_drop (tx (transaction_a), handle_a, false);
}

uint64_t nano::store::lmdb::component::count (store::transaction const & transaction_a, tables table_a) const
{
	return count (transaction_a, table_to_dbi (table_a));
}

uint64_t nano::store::lmdb::component::count (store::transaction const & transaction_a, MDB_dbi db_a) const
{
	MDB_stat stats;
	::lmdb::dbi_stat (tx (transaction_a), db_a, &stats);
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
	return !mdb_env_copy2 (env.handle (), destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void nano::store::lmdb::component::rebuild_db (store::write_transaction const & transaction_a)
{
	// Tables with uint256_union key
	std::vector<MDB_dbi> tables = { account_store.accounts_handle, block_store.blocks_handle, pruned_store.pruned_handle, confirmation_height_store.confirmation_height_handle };
	for (auto const & table : tables)
	{
		MDB_dbi temp;
		::lmdb::dbi_open (tx (transaction_a), "temp_table", MDB_CREATE, &temp);
		// Copy all values to temporary table
		for (auto i (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (std::make_unique<nano::store::lmdb::iterator<nano::uint256_union, nano::store::lmdb::db_val>> (transaction_a, env, table))), n (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (tx (transaction_a), temp, nano::store::lmdb::db_val (i->first), i->second, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, table) == count (transaction_a, temp));
		// Clear existing table
		::lmdb::dbi_drop (tx (transaction_a), table, false);
		// Put values from copy
		for (auto i (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (std::make_unique<nano::store::lmdb::iterator<nano::uint256_union, nano::store::lmdb::db_val>> (transaction_a, env, temp))), n (store::iterator<nano::uint256_union, nano::store::lmdb::db_val> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (tx (transaction_a), table, nano::store::lmdb::db_val (i->first), i->second, MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, table) == count (transaction_a, temp));
		// Remove temporary table
		::lmdb::dbi_drop (tx (transaction_a), temp, true);
	}
	// Pending table
	{
		MDB_dbi temp;
		::lmdb::dbi_open (tx (transaction_a), "temp_table", MDB_CREATE, &temp);
		// Copy all values to temporary table
		for (auto i (store::iterator<nano::pending_key, nano::pending_info> (std::make_unique<nano::store::lmdb::iterator<nano::pending_key, nano::pending_info>> (transaction_a, env, pending_store.pending_handle))), n (store::iterator<nano::pending_key, nano::pending_info> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (tx (transaction_a), temp, nano::store::lmdb::db_val (i->first), nano::store::lmdb::db_val (i->second), MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		::lmdb::dbi_drop (tx (transaction_a), pending_store.pending_handle, false);
		// Put values from copy
		for (auto i (store::iterator<nano::pending_key, nano::pending_info> (std::make_unique<nano::store::lmdb::iterator<nano::pending_key, nano::pending_info>> (transaction_a, env, temp))), n (store::iterator<nano::pending_key, nano::pending_info> (nullptr)); i != n; ++i)
		{
			auto s = mdb_put (tx (transaction_a), pending_store.pending_handle, nano::store::lmdb::db_val (i->first), nano::store::lmdb::db_val (i->second), MDB_APPEND);
			release_assert_success (s);
		}
		release_assert (count (transaction_a, pending_store.pending_handle) == count (transaction_a, temp));
		::lmdb::dbi_drop (tx (transaction_a), temp, true);
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
