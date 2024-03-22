#include <nano/lib/blocks.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/store/rocksdb/iterator.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>
#include <nano/store/version.hpp>

#include <boost/format.hpp>
#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/ptree.hpp>

#include <rocksdb/merge_operator.h>
#include <rocksdb/slice.h>
#include <rocksdb/slice_transform.h>
#include <rocksdb/utilities/backup_engine.h>
#include <rocksdb/utilities/transaction.h>

namespace
{
class event_listener : public rocksdb::EventListener
{
public:
	event_listener (std::function<void (rocksdb::FlushJobInfo const &)> const & flush_completed_cb_a) :
		flush_completed_cb (flush_completed_cb_a)
	{
	}

	void OnFlushCompleted (rocksdb::DB * /* db_a */, rocksdb::FlushJobInfo const & flush_info_a) override
	{
		flush_completed_cb (flush_info_a);
	}

private:
	std::function<void (rocksdb::FlushJobInfo const &)> flush_completed_cb;
};
}

nano::store::rocksdb::component::component (nano::logger & logger_a, std::filesystem::path const & path_a, nano::ledger_constants & constants, nano::rocksdb_config const & rocksdb_config_a, bool open_read_only_a) :
	// clang-format off
	nano::store::component{
		block_store,
		frontier_store,
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
	frontier_store{ *this },
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
	constants{ constants },
	rocksdb_config{ rocksdb_config_a },
	max_block_write_batch_num_m{ nano::narrow_cast<unsigned> (blocks_memtable_size_bytes () / (2 * (sizeof (nano::block_type) + nano::state_block::size + nano::block_sideband::size (nano::block_type::state)))) },
	cf_name_table_map{ create_cf_name_table_map () }
{
	boost::system::error_code error_mkdir, error_chmod;
	std::filesystem::create_directories (path_a, error_mkdir);
	nano::set_secure_perm_directory (path_a, error_chmod);
	error = static_cast<bool> (error_mkdir);

	if (error)
	{
		return;
	}

	debug_assert (path_a.filename () == "rocksdb");

	generate_tombstone_map ();
	small_table_factory.reset (::rocksdb::NewBlockBasedTableFactory (get_small_table_options ()));

	// TODO: get_db_options () registers a listener for resetting tombstones, needs to check if it is a problem calling it more than once.
	auto options = get_db_options ();

	// The only certain column family is "meta" which contains the DB version info.
	// RocksDB requires this operation to be in read-only mode.
	auto is_fresh_db = false;
	open (is_fresh_db, path_a, true, options, get_single_column_family ("meta"));

	auto is_fully_upgraded = false;
	if (!is_fresh_db)
	{
		auto transaction = tx_begin_read ();
		auto version_l = version.get (transaction);
		if (version_l > version_current)
		{
			logger.critical (nano::log::type::rocksdb, "The version of the ledger ({}) is too high for this node", version_l);

			error = true;
			return;
		}
		else if (version_l < version_minimum)
		{
			logger.critical (nano::log::type::rocksdb, "The version of the ledger ({}) is lower than the minimum ({}) which is supported for upgrades. Either upgrade a node first or delete the ledger.", version_l, version_minimum);

			error = true;
			return;
		}
		is_fully_upgraded = (version_l == version_current);
	}

	if (db)
	{
		// Needs to clear the store references before reopening the DB.
		handles.clear ();
		db.reset (nullptr);
	}

	if (!open_read_only_a)
	{
		construct_column_family_mutexes ();
	}

	if (is_fully_upgraded)
	{
		open (error, path_a, open_read_only_a, options, create_column_families ());
		return;
	}

	if (open_read_only_a)
	{
		// Either following cases cannot run in read-only mode:
		// a) there is no database yet, the access needs to be in write mode for it to be created;
		// b) it will upgrade, and it is not possible to do it in read-only mode.
		error = true;
		return;
	}

	if (is_fresh_db)
	{
		open (error, path_a, open_read_only_a, options, create_column_families ());
		if (!error)
		{
			version.put (tx_begin_write (), version_current); // It is fresh, someone needs to tell it its version.
		}
		return;
	}

	// The database is not upgraded, and it may not be compatible with the current column family set.
	open (error, path_a, open_read_only_a, options, get_current_column_families (path_a.string (), options));
	if (!error)
	{
		logger.info (nano::log::type::rocksdb, "Upgrade in progress...");

		auto transaction = tx_begin_write ();
		error |= do_upgrades (transaction);
	}
}

std::unordered_map<char const *, nano::tables> nano::store::rocksdb::component::create_cf_name_table_map () const
{
	std::unordered_map<char const *, nano::tables> map{ { ::rocksdb::kDefaultColumnFamilyName.c_str (), tables::default_unused },
		{ "frontiers", tables::frontiers },
		{ "accounts", tables::accounts },
		{ "blocks", tables::blocks },
		{ "pending", tables::pending },
		{ "vote", tables::vote },
		{ "online_weight", tables::online_weight },
		{ "meta", tables::meta },
		{ "peers", tables::peers },
		{ "confirmation_height", tables::confirmation_height },
		{ "pruned", tables::pruned },
		{ "final_votes", tables::final_votes },
		{ "rep_weights", tables::rep_weights } };

	debug_assert (map.size () == all_tables ().size () + 1);
	return map;
}

void nano::store::rocksdb::component::open (bool & error_a, std::filesystem::path const & path_a, bool open_read_only_a, ::rocksdb::Options const & options_a, std::vector<::rocksdb::ColumnFamilyDescriptor> column_families)
{
	//	auto options = get_db_options ();
	::rocksdb::Status s;

	std::vector<::rocksdb::ColumnFamilyHandle *> handles_l;
	if (open_read_only_a)
	{
		::rocksdb::DB * db_l;
		s = ::rocksdb::DB::OpenForReadOnly (options_a, path_a.string (), column_families, &handles_l, &db_l);
		db.reset (db_l);
	}
	else
	{
		s = ::rocksdb::OptimisticTransactionDB::Open (options_a, path_a.string (), column_families, &handles_l, &optimistic_db);
		if (optimistic_db)
		{
			db.reset (optimistic_db);
		}
	}

	handles.resize (handles_l.size ());
	for (auto i = 0; i < handles_l.size (); ++i)
	{
		handles[i].reset (handles_l[i]);
	}

	// Assign handles to supplied
	error_a |= !s.ok ();
}

bool nano::store::rocksdb::component::do_upgrades (store::write_transaction const & transaction_a)
{
	bool error_l{ false };
	auto version_l = version.get (transaction_a);
	switch (version_l)
	{
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
			release_assert (false && "do_upgrades () for RocksDB requires the version_minimum already checked.");
			error_l = true;
			break;
		case 14:
		case 15:
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
			upgrade_v21_to_v22 (transaction_a);
			[[fallthrough]];
		case 22:
			upgrade_v22_to_v23 (transaction_a);
			[[fallthrough]];
		case 23:
			break;
		default:
			logger.critical (nano::log::type::rocksdb, "The version of the ledger ({}) is too high for this node", version_l);
			error_l = true;
			break;
	}
	return error_l;
}

void nano::store::rocksdb::component::upgrade_v21_to_v22 (store::write_transaction const & transaction_a)
{
	logger.info (nano::log::type::rocksdb, "Upgrading database from v21 to v22...");

	if (column_family_exists ("unchecked"))
	{
		auto const unchecked_handle = get_column_family ("unchecked");
		db->DropColumnFamily (unchecked_handle);
		db->DestroyColumnFamilyHandle (unchecked_handle);
		std::erase_if (handles, [unchecked_handle] (auto & handle) {
			if (handle.get () == unchecked_handle)
			{
				// The handle resource is deleted by RocksDB.
				[[maybe_unused]] auto ptr = handle.release ();
				return true;
			}
			return false;
		});
		logger.debug (nano::log::type::rocksdb, "Finished removing unchecked table");
	}

	version.put (transaction_a, 22);

	logger.info (nano::log::type::rocksdb, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void nano::store::rocksdb::component::upgrade_v22_to_v23 (store::write_transaction const & transaction_a)
{
	logger.info (nano::log::type::rocksdb, "Upgrading database from v22 to v23...");
	auto i{ make_iterator<nano::account, nano::account_info_v22> (transaction_a, tables::accounts) };
	auto end{ store::iterator<nano::account, nano::account_info_v22> (nullptr) };
	uint64_t processed_accounts = 0;
	for (; i != end; ++i)
	{
		if (!i->second.balance.is_zero ())
		{
			nano::uint128_t total{ 0 };
			nano::store::rocksdb::db_val value;
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
			logger.info (nano::log::type::lmdb, "processed {} accounts", processed_accounts);
		}
	}
	logger.info (nano::log::type::lmdb, "processed {} accounts", processed_accounts);
	version.put (transaction_a, 23);
	logger.info (nano::log::type::rocksdb, "Upgrading database from v22 to v23 completed");
}

void nano::store::rocksdb::component::generate_tombstone_map ()
{
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::blocks), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::accounts), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::pending), std::forward_as_tuple (0, 25000));
}

rocksdb::ColumnFamilyOptions nano::store::rocksdb::component::get_common_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const
{
	::rocksdb::ColumnFamilyOptions cf_options;
	cf_options.table_factory = table_factory_a;

	// (1 active, 1 inactive)
	auto num_memtables = 2;

	// Each level is a multiple of the above. If L1 is 512MB. L2 will be 512 * 8 = 2GB. L3 will be 2GB * 8 = 16GB, and so on...
	cf_options.max_bytes_for_level_multiplier = 8;

	// Although this should be the default provided by RocksDB, not setting this is causing sequence conflict checks if not using
	cf_options.max_write_buffer_size_to_maintain = memtable_size_bytes_a * num_memtables;

	// Files older than this (1 day) will be scheduled for compaction when there is no other background work. This can lead to more writes however.
	cf_options.ttl = 1 * 24 * 60 * 60;

	// Multiplier for each level
	cf_options.target_file_size_multiplier = 10;

	// Size of level 1 sst files
	cf_options.target_file_size_base = memtable_size_bytes_a;

	// Size of each memtable
	cf_options.write_buffer_size = memtable_size_bytes_a;

	// Number of memtables to keep in memory
	cf_options.max_write_buffer_number = num_memtables;

	return cf_options;
}

rocksdb::ColumnFamilyOptions nano::store::rocksdb::component::get_cf_options (std::string const & cf_name_a) const
{
	::rocksdb::ColumnFamilyOptions cf_options;
	auto const memtable_size_bytes = base_memtable_size_bytes ();
	auto const block_cache_size_bytes = 1024ULL * 1024 * rocksdb_config.memory_multiplier * base_block_cache_size;
	if (cf_name_a == "blocks")
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 4)));
		cf_options = get_active_cf_options (table_factory, blocks_memtable_size_bytes ());
	}
	else if (cf_name_a == "confirmation_height")
	{
		// Entries will not be deleted in the normal case, so can make memtables a lot bigger
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes * 2);
	}
	else if (cf_name_a == "meta" || cf_name_a == "online_weight" || cf_name_a == "peers")
	{
		// Meta - It contains just version key
		// Online weight - Periodically deleted
		// Peers - Cleaned periodically, a lot of deletions. This is never read outside of initializing? Keep this small
		cf_options = get_small_cf_options (small_table_factory);
	}
	else if (cf_name_a == "cached_counts")
	{
		// Really small (keys are blocks tables, value is uint64_t)
		cf_options = get_small_cf_options (small_table_factory);
	}
	else if (cf_name_a == "pending")
	{
		// Pending can have a lot of deletions too
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);

		// Number of files in level 0 which triggers compaction. Size of L0 and L1 should be kept similar as this is the only compaction which is single threaded
		cf_options.level0_file_num_compaction_trigger = 2;

		// L1 size, compaction is triggered for L0 at this size (2 SST files in L1)
		cf_options.max_bytes_for_level_base = memtable_size_bytes * 2;
	}
	else if (cf_name_a == "frontiers")
	{
		// Frontiers is only needed during bootstrap for legacy blocks
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == "accounts")
	{
		// Can have deletions from rollbacks
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 2)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == "vote")
	{
		// No deletes it seems, only overwrites.
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 2)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == "pruned")
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 2)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == "final_votes")
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 2)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == "rep_weights")
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 2)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);
	}
	else if (cf_name_a == ::rocksdb::kDefaultColumnFamilyName)
	{
		// Do nothing.
	}
	else
	{
		debug_assert (false);
	}

	return cf_options;
}

std::vector<rocksdb::ColumnFamilyDescriptor> nano::store::rocksdb::component::create_column_families ()
{
	std::vector<::rocksdb::ColumnFamilyDescriptor> column_families;
	for (auto & [cf_name, table] : cf_name_table_map)
	{
		(void)table;
		column_families.emplace_back (cf_name, get_cf_options (cf_name));
	}
	return column_families;
}

nano::store::write_transaction nano::store::rocksdb::component::tx_begin_write (std::vector<nano::tables> const & tables_requiring_locks_a, std::vector<nano::tables> const & tables_no_locks_a)
{
	std::unique_ptr<nano::store::rocksdb::write_transaction_impl> txn;
	release_assert (optimistic_db != nullptr);
	if (tables_requiring_locks_a.empty () && tables_no_locks_a.empty ())
	{
		// Use all tables if none are specified
		txn = std::make_unique<nano::store::rocksdb::write_transaction_impl> (optimistic_db, all_tables (), tables_no_locks_a, write_lock_mutexes);
	}
	else
	{
		txn = std::make_unique<nano::store::rocksdb::write_transaction_impl> (optimistic_db, tables_requiring_locks_a, tables_no_locks_a, write_lock_mutexes);
	}

	// Tables must be kept in alphabetical order. These can be used for mutex locking, so order is important to prevent deadlocking
	debug_assert (std::is_sorted (tables_requiring_locks_a.begin (), tables_requiring_locks_a.end ()));

	return store::write_transaction{ std::move (txn) };
}

nano::store::read_transaction nano::store::rocksdb::component::tx_begin_read () const
{
	return store::read_transaction{ std::make_unique<nano::store::rocksdb::read_transaction_impl> (db.get ()) };
}

std::string nano::store::rocksdb::component::vendor_get () const
{
	return boost::str (boost::format ("RocksDB %1%.%2%.%3%") % ROCKSDB_MAJOR % ROCKSDB_MINOR % ROCKSDB_PATCH);
}

std::vector<::rocksdb::ColumnFamilyDescriptor> nano::store::rocksdb::component::get_single_column_family (std::string cf_name) const
{
	std::vector<::rocksdb::ColumnFamilyDescriptor> minimum_cf_set{
		{ ::rocksdb::kDefaultColumnFamilyName, ::rocksdb::ColumnFamilyOptions{} },
		{ cf_name, get_cf_options (cf_name) }
	};
	return minimum_cf_set;
}

std::vector<::rocksdb::ColumnFamilyDescriptor> nano::store::rocksdb::component::get_current_column_families (std::string const & path_a, ::rocksdb::Options const & options_a) const
{
	std::vector<::rocksdb::ColumnFamilyDescriptor> column_families;

	// Retrieve the column families available in the database.
	std::vector<std::string> current_cf_names;
	auto s = ::rocksdb::DB::ListColumnFamilies (options_a, path_a, &current_cf_names);
	debug_assert (s.ok ());

	column_families.reserve (current_cf_names.size ());
	for (const auto & cf : current_cf_names)
	{
		column_families.emplace_back (cf, ::rocksdb::ColumnFamilyOptions ());
	}

	return column_families;
}

rocksdb::ColumnFamilyHandle * nano::store::rocksdb::component::get_column_family (char const * name) const
{
	auto & handles_l = handles;
	auto iter = std::find_if (handles_l.begin (), handles_l.end (), [name] (auto & handle) {
		return (handle->GetName () == name);
	});
	debug_assert (iter != handles_l.end ());
	return (*iter).get ();
}

bool nano::store::rocksdb::component::column_family_exists (char const * name) const
{
	auto & handles_l = handles;
	auto iter = std::find_if (handles_l.begin (), handles_l.end (), [name] (auto & handle) {
		return (handle->GetName () == name);
	});
	return (iter != handles_l.end ());
}

rocksdb::ColumnFamilyHandle * nano::store::rocksdb::component::table_to_column_family (tables table_a) const
{
	switch (table_a)
	{
		case tables::frontiers:
			return get_column_family ("frontiers");
		case tables::accounts:
			return get_column_family ("accounts");
		case tables::blocks:
			return get_column_family ("blocks");
		case tables::pending:
			return get_column_family ("pending");
		case tables::vote:
			return get_column_family ("vote");
		case tables::online_weight:
			return get_column_family ("online_weight");
		case tables::meta:
			return get_column_family ("meta");
		case tables::peers:
			return get_column_family ("peers");
		case tables::pruned:
			return get_column_family ("pruned");
		case tables::confirmation_height:
			return get_column_family ("confirmation_height");
		case tables::final_votes:
			return get_column_family ("final_votes");
		case tables::rep_weights:
			return get_column_family ("rep_weights");
		default:
			release_assert (false);
			return get_column_family ("");
	}
}

bool nano::store::rocksdb::component::exists (store::transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a) const
{
	::rocksdb::PinnableSlice slice;
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db->Get (snapshot_options (transaction_a), table_to_column_family (table_a), key_a, &slice);
	}
	else
	{
		::rocksdb::ReadOptions options;
		options.fill_cache = false;
		status = tx (transaction_a)->Get (options, table_to_column_family (table_a), key_a, &slice);
	}

	return (status.ok ());
}

int nano::store::rocksdb::component::del (store::write_transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a)
{
	debug_assert (transaction_a.contains (table_a));
	// RocksDB does not report not_found status, it is a pre-condition that the key exists
	debug_assert (exists (transaction_a, table_a, key_a));
	flush_tombstones_check (table_a);
	return tx (transaction_a)->Delete (table_to_column_family (table_a), key_a).code ();
}

void nano::store::rocksdb::component::flush_tombstones_check (tables table_a)
{
	// Update the number of deletes for some tables, and force a flush if there are too many tombstones
	// as it can affect read performance.
	if (auto it = tombstone_map.find (table_a); it != tombstone_map.end ())
	{
		auto & tombstone_info = it->second;
		if (++tombstone_info.num_since_last_flush > tombstone_info.max)
		{
			tombstone_info.num_since_last_flush = 0;
			flush_table (table_a);
		}
	}
}

void nano::store::rocksdb::component::flush_table (nano::tables table_a)
{
	db->Flush (::rocksdb::FlushOptions{}, table_to_column_family (table_a));
}

rocksdb::Transaction * nano::store::rocksdb::component::tx (store::transaction const & transaction_a) const
{
	debug_assert (!is_read (transaction_a));
	return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
}

int nano::store::rocksdb::component::get (store::transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val & value_a) const
{
	::rocksdb::ReadOptions options;
	::rocksdb::PinnableSlice slice;
	auto handle = table_to_column_family (table_a);
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db->Get (snapshot_options (transaction_a), handle, key_a, &slice);
	}
	else
	{
		status = tx (transaction_a)->Get (options, handle, key_a, &slice);
	}

	if (status.ok ())
	{
		value_a.buffer = std::make_shared<std::vector<uint8_t>> (slice.size ());
		std::memcpy (value_a.buffer->data (), slice.data (), slice.size ());
		value_a.convert_buffer_to_value ();
	}
	return status.code ();
}

int nano::store::rocksdb::component::put (store::write_transaction const & transaction_a, tables table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val const & value_a)
{
	debug_assert (transaction_a.contains (table_a));
	auto txn = tx (transaction_a);
	return txn->Put (table_to_column_family (table_a), key_a, value_a).code ();
}

bool nano::store::rocksdb::component::not_found (int status) const
{
	return (status_code_not_found () == status);
}

bool nano::store::rocksdb::component::success (int status) const
{
	return (static_cast<int> (::rocksdb::Status::Code::kOk) == status);
}

int nano::store::rocksdb::component::status_code_not_found () const
{
	return static_cast<int> (::rocksdb::Status::Code::kNotFound);
}

uint64_t nano::store::rocksdb::component::count (store::transaction const & transaction_a, tables table_a) const
{
	uint64_t sum = 0;
	// Peers/online weight are small enough that they can just be iterated to get accurate counts.
	if (table_a == tables::peers)
	{
		for (auto i (peer.begin (transaction_a)), n (peer.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	else if (table_a == tables::online_weight)
	{
		for (auto i (online_weight.begin (transaction_a)), n (online_weight.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	// This should be correct at node start, later only cache should be used
	else if (table_a == tables::pruned)
	{
		db->GetIntProperty (table_to_column_family (table_a), "rocksdb.estimate-num-keys", &sum);
	}
	// This should be accurate as long as there continues to be no deletes or duplicate entries.
	else if (table_a == tables::final_votes)
	{
		db->GetIntProperty (table_to_column_family (table_a), "rocksdb.estimate-num-keys", &sum);
	}
	// Accounts and blocks should only be used in tests and CLI commands to check database consistency
	// otherwise there can be performance issues.
	else if (table_a == tables::accounts)
	{
		for (auto i (account.begin (transaction_a)), n (account.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	else if (table_a == tables::blocks)
	{
		// This is also used in some CLI commands
		for (auto i (block.begin (transaction_a)), n (block.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	else if (table_a == tables::confirmation_height)
	{
		for (auto i (confirmation_height.begin (transaction_a)), n (confirmation_height.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	// rep_weights should only be used in tests otherwise there can be performance issues.
	else if (table_a == tables::rep_weights)
	{
		for (auto i (rep_weight.begin (transaction_a)), n (rep_weight.end ()); i != n; ++i)
		{
			++sum;
		}
	}
	else
	{
		debug_assert (false);
		db->GetIntProperty (table_to_column_family (table_a), "rocksdb.estimate-num-keys", &sum);
	}

	return sum;
}

int nano::store::rocksdb::component::drop (store::write_transaction const & transaction_a, tables table_a)
{
	debug_assert (transaction_a.contains (table_a));
	auto col = table_to_column_family (table_a);

	int status = static_cast<int> (::rocksdb::Status::Code::kOk);
	if (success (status))
	{
		// Dropping/Creating families like in node::ongoing_peer_clear can cause write stalls, just delete them manually.
		if (table_a == tables::peers)
		{
			int status = 0;
			for (auto i = peer.begin (transaction_a), n = peer.end (); i != n; ++i)
			{
				status = del (transaction_a, tables::peers, nano::store::rocksdb::db_val (i->first));
				release_assert (success (status));
			}
			return status;
		}
		else
		{
			return clear (col);
		}
	}
	return status;
}

int nano::store::rocksdb::component::clear (::rocksdb::ColumnFamilyHandle * column_family)
{
	::rocksdb::ReadOptions read_options;
	::rocksdb::WriteOptions write_options;
	::rocksdb::WriteBatch write_batch;
	std::unique_ptr<::rocksdb::Iterator> it (db->NewIterator (read_options, column_family));

	for (it->SeekToFirst (); it->Valid (); it->Next ())
	{
		write_batch.Delete (column_family, it->key ());
	}

	::rocksdb::Status status = db->Write (write_options, &write_batch);
	release_assert (status.ok ());

	return status.code ();
}

void nano::store::rocksdb::component::construct_column_family_mutexes ()
{
	for (auto table : all_tables ())
	{
		write_lock_mutexes.emplace (std::piecewise_construct, std::forward_as_tuple (table), std::forward_as_tuple ());
	}
}

rocksdb::Options nano::store::rocksdb::component::get_db_options ()
{
	::rocksdb::Options db_options;
	db_options.create_if_missing = true;
	db_options.create_missing_column_families = true;

	// TODO: review if this should be changed due to the unchecked table removal.
	// Enable whole key bloom filter in memtables for ones with memtable_prefix_bloom_size_ratio set (unchecked table currently).
	// It can potentially reduce CPU usage for point-look-ups.
	db_options.memtable_whole_key_filtering = true;

	// Sets the compaction priority
	db_options.compaction_pri = ::rocksdb::CompactionPri::kMinOverlappingRatio;

	// Start aggressively flushing WAL files when they reach over 1GB
	db_options.max_total_wal_size = 1 * 1024 * 1024 * 1024LL;

	// Optimize RocksDB. This is the easiest way to get RocksDB to perform well
	db_options.IncreaseParallelism (rocksdb_config.io_threads);
	db_options.OptimizeLevelStyleCompaction ();

	// Adds a separate write queue for memtable/WAL
	db_options.enable_pipelined_write = true;

	// Default is 16, setting to -1 allows faster startup times for SSDs by allowings more files to be read in parallel.
	db_options.max_file_opening_threads = -1;

	// The MANIFEST file contains a history of all file operations since the last time the DB was opened and is replayed during DB open.
	// Default is 1GB, lowering this to avoid replaying for too long (100MB)
	db_options.max_manifest_file_size = 100 * 1024 * 1024ULL;

	// Not compressing any SST files for compatibility reasons.
	db_options.compression = ::rocksdb::kNoCompression;

	auto event_listener_l = new event_listener ([this] (::rocksdb::FlushJobInfo const & flush_job_info_a) {
		this->on_flush (flush_job_info_a);
	});
	db_options.listeners.emplace_back (event_listener_l);

	return db_options;
}

rocksdb::BlockBasedTableOptions nano::store::rocksdb::component::get_active_table_options (std::size_t lru_size) const
{
	::rocksdb::BlockBasedTableOptions table_options;

	// Improve point lookup performance be using the data block hash index (uses about 5% more space).
	table_options.data_block_index_type = ::rocksdb::BlockBasedTableOptions::DataBlockIndexType::kDataBlockBinaryAndHash;
	table_options.data_block_hash_table_util_ratio = 0.75;

	// Using format_version=4 significantly reduces the index block size, in some cases around 4-5x.
	// This frees more space in block cache, which would result in higher hit rate for data and filter blocks,
	// or offer the same performance with a smaller block cache size.
	table_options.format_version = 4;
	table_options.index_block_restart_interval = 16;

	// Block cache for reads
	table_options.block_cache = ::rocksdb::NewLRUCache (lru_size);

	// Bloom filter to help with point reads. 10bits gives 1% false positive rate.
	table_options.filter_policy.reset (::rocksdb::NewBloomFilterPolicy (10, false));

	// Increasing block_size decreases memory usage and space amplification, but increases read amplification.
	table_options.block_size = 16 * 1024ULL;

	// Whether level 0 index and filter blocks are stored in block_cache
	table_options.pin_l0_filter_and_index_blocks_in_cache = true;

	return table_options;
}

rocksdb::BlockBasedTableOptions nano::store::rocksdb::component::get_small_table_options () const
{
	::rocksdb::BlockBasedTableOptions table_options;
	// Improve point lookup performance be using the data block hash index (uses about 5% more space).
	table_options.data_block_index_type = ::rocksdb::BlockBasedTableOptions::DataBlockIndexType::kDataBlockBinaryAndHash;
	table_options.data_block_hash_table_util_ratio = 0.75;
	table_options.block_size = 1024ULL;
	return table_options;
}

rocksdb::ColumnFamilyOptions nano::store::rocksdb::component::get_small_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a) const
{
	auto const memtable_size_bytes = 10000;
	auto cf_options = get_common_cf_options (table_factory_a, memtable_size_bytes);

	// Number of files in level 0 which triggers compaction. Size of L0 and L1 should be kept similar as this is the only compaction which is single threaded
	cf_options.level0_file_num_compaction_trigger = 1;

	// L1 size, compaction is triggered for L0 at this size (1 SST file in L1)
	cf_options.max_bytes_for_level_base = memtable_size_bytes;

	return cf_options;
}

::rocksdb::ColumnFamilyOptions nano::store::rocksdb::component::get_active_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const
{
	auto cf_options = get_common_cf_options (table_factory_a, memtable_size_bytes_a);

	// Number of files in level 0 which triggers compaction. Size of L0 and L1 should be kept similar as this is the only compaction which is single threaded
	cf_options.level0_file_num_compaction_trigger = 4;

	// L1 size, compaction is triggered for L0 at this size (4 SST files in L1)
	cf_options.max_bytes_for_level_base = memtable_size_bytes_a * 4;

	// Size target of levels are changed dynamically based on size of the last level
	cf_options.level_compaction_dynamic_level_bytes = true;

	return cf_options;
}

void nano::store::rocksdb::component::on_flush (::rocksdb::FlushJobInfo const & flush_job_info_a)
{
	// Reset appropriate tombstone counters
	if (auto it = tombstone_map.find (cf_name_table_map[flush_job_info_a.cf_name.c_str ()]); it != tombstone_map.end ())
	{
		it->second.num_since_last_flush = 0;
	}
}

std::vector<nano::tables> nano::store::rocksdb::component::all_tables () const
{
	return std::vector<nano::tables>{ tables::accounts, tables::blocks, tables::confirmation_height, tables::final_votes, tables::frontiers, tables::meta, tables::online_weight, tables::peers, tables::pending, tables::pruned, tables::vote, tables::rep_weights };
}

bool nano::store::rocksdb::component::copy_db (std::filesystem::path const & destination_path)
{
	std::unique_ptr<::rocksdb::BackupEngine> backup_engine;
	{
		::rocksdb::BackupEngine * backup_engine_raw;
		::rocksdb::BackupEngineOptions backup_options (destination_path.string ());
		// Use incremental backups (default)
		backup_options.share_table_files = true;

		// Increase number of threads used for copying
		backup_options.max_background_operations = nano::hardware_concurrency ();
		auto status = ::rocksdb::BackupEngine::Open (::rocksdb::Env::Default (), backup_options, &backup_engine_raw);
		backup_engine.reset (backup_engine_raw);
		if (!status.ok ())
		{
			return false;
		}
	}

	auto status = backup_engine->CreateNewBackup (db.get ());
	if (!status.ok ())
	{
		return false;
	}

	std::vector<::rocksdb::BackupInfo> backup_infos;
	backup_engine->GetBackupInfo (&backup_infos);

	for (auto const & backup_info : backup_infos)
	{
		status = backup_engine->VerifyBackup (backup_info.backup_id);
		if (!status.ok ())
		{
			return false;
		}
	}

	{
		std::unique_ptr<::rocksdb::BackupEngineReadOnly> backup_engine_read;
		{
			::rocksdb::BackupEngineReadOnly * backup_engine_read_raw;
			status = ::rocksdb::BackupEngineReadOnly::Open (::rocksdb::Env::Default (), ::rocksdb::BackupEngineOptions (destination_path.string ()), &backup_engine_read_raw);
		}
		if (!status.ok ())
		{
			return false;
		}

		// First remove all files (not directories) in the destination
		for (auto const & path : std::filesystem::directory_iterator (destination_path))
		{
			if (std::filesystem::is_regular_file (path))
			{
				std::filesystem::remove (path);
			}
		}

		// Now generate the relevant files from the backup
		status = backup_engine->RestoreDBFromLatestBackup (destination_path.string (), destination_path.string ());
	}

	// Open it so that it flushes all WAL files
	if (status.ok ())
	{
		nano::store::rocksdb::component rocksdb_store{ logger, destination_path.string (), constants, rocksdb_config, false };
		return !rocksdb_store.init_error ();
	}
	return false;
}

void nano::store::rocksdb::component::rebuild_db (store::write_transaction const & transaction_a)
{
	// Not available for RocksDB
}

bool nano::store::rocksdb::component::init_error () const
{
	return error;
}

void nano::store::rocksdb::component::serialize_memory_stats (boost::property_tree::ptree & json)
{
	uint64_t val;

	// Approximate size of active and unflushed immutable memtables (bytes).
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kCurSizeAllMemTables, &val);
	json.put ("cur-size-all-mem-tables", val);

	// Approximate size of active, unflushed immutable, and pinned immutable memtables (bytes).
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kSizeAllMemTables, &val);
	json.put ("size-all-mem-tables", val);

	// Estimated memory used for reading SST tables, excluding memory used in block cache (e.g. filter and index blocks).
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateTableReadersMem, &val);
	json.put ("estimate-table-readers-mem", val);

	//  An estimate of the amount of live data in bytes.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateLiveDataSize, &val);
	json.put ("estimate-live-data-size", val);

	//  Returns 1 if at least one compaction is pending; otherwise, returns 0.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kCompactionPending, &val);
	json.put ("compaction-pending", val);

	// Estimated number of total keys in the active and unflushed immutable memtables and storage.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateNumKeys, &val);
	json.put ("estimate-num-keys", val);

	// Estimated total number of bytes compaction needs to rewrite to get all levels down
	// to under target size. Not valid for other compactions than level-based.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimatePendingCompactionBytes, &val);
	json.put ("estimate-pending-compaction-bytes", val);

	//  Total size (bytes) of all SST files.
	//  WARNING: may slow down online queries if there are too many files.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kTotalSstFilesSize, &val);
	json.put ("total-sst-files-size", val);

	// Block cache capacity.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBlockCacheCapacity, &val);
	json.put ("block-cache-capacity", val);

	// Memory size for the entries residing in block cache.
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBlockCacheUsage, &val);
	json.put ("block-cache-usage", val);
}

unsigned long long nano::store::rocksdb::component::blocks_memtable_size_bytes () const
{
	return base_memtable_size_bytes ();
}

unsigned long long nano::store::rocksdb::component::base_memtable_size_bytes () const
{
	return 1024ULL * 1024 * rocksdb_config.memory_multiplier * base_memtable_size;
}

// This is a ratio of the blocks memtable size to keep total write transaction commit size down.
unsigned nano::store::rocksdb::component::max_block_write_batch_num () const
{
	return max_block_write_batch_num_m;
}

std::string nano::store::rocksdb::component::error_string (int status) const
{
	return std::to_string (status);
}

nano::store::rocksdb::component::tombstone_info::tombstone_info (uint64_t num_since_last_flush_a, uint64_t const max_a) :
	num_since_last_flush (num_since_last_flush_a),
	max (max_a)
{
}
