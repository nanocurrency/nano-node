#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/store/rocksdb/backend_rocksdb.hpp>
#include <nano/store/rocksdb/iterator.hpp>
#include <nano/store/rocksdb/transaction_rocksdb.hpp>
#include <nano/store/rocksdb/utility.hpp>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>

#include <set>

#include <rocksdb/filter_policy.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/backup_engine.h>
#include <rocksdb/utilities/transaction.h>
#include <rocksdb/utilities/write_batch_with_index.h>

namespace
{
class event_listener : public ::rocksdb::EventListener
{
public:
	event_listener (std::function<void (::rocksdb::FlushJobInfo const &)> const & flush_completed_cb_a) :
		flush_completed_cb (flush_completed_cb_a)
	{
	}

	void OnFlushCompleted (::rocksdb::DB * /* db_a */, ::rocksdb::FlushJobInfo const & flush_info_a) override
	{
		flush_completed_cb (flush_info_a);
	}

private:
	std::function<void (::rocksdb::FlushJobInfo const &)> flush_completed_cb;
};

// Checks if status indicates database/path doesn't exist
bool is_not_found (::rocksdb::Status const & status)
{
	if (status.IsNotFound ())
	{
		return true;
	}
	if (status.IsIOError () && status.subcode () == ::rocksdb::Status::kPathNotFound)
	{
		return true;
	}
	return false;
}
}

namespace nano::store::rocksdb
{
backend_rocksdb::backend_rocksdb (std::filesystem::path const & path, nano::rocksdb_config const & config_a, nano::logger & logger_a, nano::store::txn_tracking_config const & txn_tracking_config_a) :
	backend{ logger_a, txn_tracking_config_a },
	database_path{ path },
	config{ config_a }
{
	generate_tombstone_map ();
}

backend_rocksdb::~backend_rocksdb ()
{
	close ();
}

void backend_rocksdb::close_impl ()
{
	// Close all table handles first
	table_handles.clear ();
	handles.clear ();

	// Release database pointer (rocksdb closes db in the destructor)
	db.reset ();
	transaction_db = nullptr;
}

void backend_rocksdb::open_impl (column_schema schema, nano::store::open_mode mode)
{
	// Create database directory if needed
	if (mode != nano::store::open_mode::read_only)
	{
		boost::system::error_code error_mkdir, error_chmod;
		std::filesystem::create_directories (database_path, error_mkdir);
		nano::set_secure_perm_directory (database_path, error_chmod);

		if (error_mkdir)
		{
			throw std::runtime_error ("Failed to create database directory: " + database_path.string ());
		}
	}

	auto const options = get_db_options (mode);

	// Get existing column families from the database (if it exists)
	std::vector<std::string> existing_cf_names;
	auto list_status = ::rocksdb::DB::ListColumnFamilies (options, database_path.string (), &existing_cf_names);

	// If database doesn't exist all column families will be created
	if (!list_status.ok ())
	{
		if (is_not_found (list_status))
		{
			debug_assert (existing_cf_names.empty ());
		}
		else
		{
			throw std::runtime_error ("Failed to list existing column families: " + list_status.ToString ());
		}
	}

	// Build column family descriptors - merge existing with schema-required column families
	// RocksDB with create_missing_column_families=true will auto-create any missing ones
	std::set<std::string> cf_names_set (existing_cf_names.begin (), existing_cf_names.end ());

	// Add schema column families to the set
	for (auto const & [table, name] : schema)
	{
		cf_names_set.insert (name);
	}

	// Ensure default column family is always present
	cf_names_set.insert (::rocksdb::kDefaultColumnFamilyName);

	// Create descriptors for all column families
	std::vector<::rocksdb::ColumnFamilyDescriptor> column_families;
	for (auto const & cf_name : cf_names_set)
	{
		column_families.emplace_back (cf_name, get_cf_options (cf_name));
	}

	open_db (database_path, mode, options, column_families);

	// Build table_handles and name_to_table from schema
	for (auto const & [table, name] : schema)
	{
		table_handles[table] = get_column_family (name);
		name_to_table[name] = table;
	}
}

void backend_rocksdb::open_db (std::filesystem::path const & path, nano::store::open_mode mode, ::rocksdb::Options const & options, std::vector<::rocksdb::ColumnFamilyDescriptor> column_families)
{
	::rocksdb::Status s;

	std::vector<::rocksdb::ColumnFamilyHandle *> handles_l;
	if (mode == nano::store::open_mode::read_only)
	{
		::rocksdb::DB * db_l;
		s = ::rocksdb::DB::OpenForReadOnly (options, path.string (), column_families, &handles_l, &db_l);
		db.reset (db_l);
	}
	else
	{
		::rocksdb::TransactionDB * transaction_db_l;
		s = ::rocksdb::TransactionDB::Open (options, ::rocksdb::TransactionDBOptions{}, path.string (), column_families, &handles_l, &transaction_db_l);
		db.reset (transaction_db_l);
		transaction_db = transaction_db_l;
	}

	handles.resize (handles_l.size ());
	for (size_t i = 0; i < handles_l.size (); ++i)
	{
		handles[i].reset (handles_l[i]);
	}

	if (!s.ok ())
	{
		if (is_not_found (s))
		{
			throw nano::error (nano::error_backend::db_not_found);
		}
		throw std::runtime_error ("Failed to open RocksDB database: " + s.ToString ());
	}
}

::rocksdb::Options backend_rocksdb::get_db_options (nano::store::open_mode mode)
{
	::rocksdb::Options db_options;

	db_options.create_if_missing = (mode != nano::store::open_mode::read_only);
	db_options.create_missing_column_families = (mode != nano::store::open_mode::read_only);

	db_options.OptimizeLevelStyleCompaction ();
	db_options.IncreaseParallelism (config.io_threads);
	db_options.compression = ::rocksdb::kNoCompression;

	db_options.keep_log_file_num = config.max_log_files;

	if (config.log_level == "debug")
	{
		db_options.info_log_level = ::rocksdb::InfoLogLevel::DEBUG_LEVEL;
	}
	else if (config.log_level == "info")
	{
		db_options.info_log_level = ::rocksdb::InfoLogLevel::INFO_LEVEL;
	}
	else if (config.log_level == "warn")
	{
		db_options.info_log_level = ::rocksdb::InfoLogLevel::WARN_LEVEL;
	}
	else if (config.log_level == "error")
	{
		db_options.info_log_level = ::rocksdb::InfoLogLevel::ERROR_LEVEL;
	}
	else if (config.log_level == "fatal")
	{
		db_options.info_log_level = ::rocksdb::InfoLogLevel::FATAL_LEVEL;
	}

	auto event_listener_l = new event_listener ([this] (::rocksdb::FlushJobInfo const & flush_job_info) {
		this->on_flush (flush_job_info);
	});
	db_options.listeners.emplace_back (event_listener_l);

	return db_options;
}

::rocksdb::BlockBasedTableOptions backend_rocksdb::get_table_options () const
{
	::rocksdb::BlockBasedTableOptions table_options;

	// Improve point lookup performance be using the data block hash index (uses about 5% more space)
	table_options.data_block_index_type = ::rocksdb::BlockBasedTableOptions::DataBlockIndexType::kDataBlockBinaryAndHash;

	// Using storage format_version 5
	// Version 5 offers improved read spead, caching and better compression (if enabled)
	// Any existing ledger data in version 4 will not be migrated. New data will be written in version 5
	table_options.format_version = 5;

	// Block cache for reads
	table_options.block_cache = ::rocksdb::NewLRUCache (config.read_cache * 1024 * 1024);

	// Bloom filter to help with point reads. 10bits gives 1% false positive rate
	table_options.filter_policy.reset (::rocksdb::NewBloomFilterPolicy (10, false));

	return table_options;
}

::rocksdb::ColumnFamilyOptions backend_rocksdb::get_cf_options (std::string const & cf_name) const
{
	::rocksdb::ColumnFamilyOptions cf_options;
	if (cf_name != ::rocksdb::kDefaultColumnFamilyName)
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_table_options ()));
		cf_options.table_factory = table_factory;
		// Size of each memtable (write buffer for this column family)
		cf_options.write_buffer_size = config.write_cache * 1024 * 1024;
	}
	return cf_options;
}

::rocksdb::ColumnFamilyHandle * backend_rocksdb::table_to_column_family (nano::store::table table) const
{
	auto it = table_handles.find (table);
	release_assert (it != table_handles.end (), "table not found");
	return it->second;
}

::rocksdb::ColumnFamilyHandle * backend_rocksdb::get_column_family (std::string const & name) const
{
	auto iter = std::find_if (handles.begin (), handles.end (), [name] (auto & handle) {
		return handle->GetName () == name;
	});
	release_assert (iter != handles.end ());
	return iter->get ();
}

bool backend_rocksdb::column_family_exists (std::string const & name) const
{
	auto iter = std::find_if (handles.begin (), handles.end (), [name] (auto & handle) {
		return handle->GetName () == name;
	});
	return iter != handles.end ();
}

int backend_rocksdb::get (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val & value) const
{
	::rocksdb::PinnableSlice slice;
	auto key_slice = to_slice (key);
	auto handle = table_to_column_family (table);
	auto internals = rocksdb::tx (txn);

	auto status = std::visit ([&] (auto && ptr) {
		using V = std::remove_cvref_t<decltype (ptr)>;
		if constexpr (std::is_same_v<V, ::rocksdb::Transaction *>)
		{
			::rocksdb::ReadOptions options;
			options.snapshot = ptr->GetSnapshot ();
			return ptr->Get (options, handle, key_slice, &slice);
		}
		else if constexpr (std::is_same_v<V, ::rocksdb::ReadOptions *>)
		{
			return db->Get (*ptr, handle, key_slice, &slice);
		}
		else
		{
			static_assert (sizeof (V) == 0, "Missing variant handler for type V");
		}
	},
	internals);

	if (status.ok ())
	{
		value = from_slice (slice);
	}
	return status.code ();
}

int backend_rocksdb::put (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key, nano::store::db_val const & value)
{
	auto key_slice = to_slice (key);
	auto value_slice = to_slice (value);
	return std::get<::rocksdb::Transaction *> (rocksdb::tx (txn))->Put (table_to_column_family (table), key_slice, value_slice).code ();
}

int backend_rocksdb::del (nano::store::write_transaction const & txn, nano::store::table table, nano::store::db_val const & key)
{
	flush_tombstones_check (table);
	auto key_slice = to_slice (key);
	return std::get<::rocksdb::Transaction *> (rocksdb::tx (txn))->Delete (table_to_column_family (table), key_slice).code ();
}

bool backend_rocksdb::exists (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	::rocksdb::PinnableSlice slice;
	auto key_slice = to_slice (key);
	auto internals = rocksdb::tx (txn);

	auto status = std::visit ([&] (auto && ptr) {
		using V = std::remove_cvref_t<decltype (ptr)>;
		if constexpr (std::is_same_v<V, ::rocksdb::Transaction *>)
		{
			::rocksdb::ReadOptions options;
			options.fill_cache = false;
			options.snapshot = ptr->GetSnapshot ();
			return ptr->Get (options, table_to_column_family (table), key_slice, &slice);
		}
		else if constexpr (std::is_same_v<V, ::rocksdb::ReadOptions *>)
		{
			return db->Get (*ptr, table_to_column_family (table), key_slice, &slice);
		}
		else
		{
			static_assert (sizeof (V) == 0, "Missing variant handler for type V");
		}
	},
	internals);

	return status.ok ();
}

uint64_t backend_rocksdb::count (nano::store::transaction const & txn, nano::store::table table) const
{
	if (count_is_exact (table))
	{
		return count_exact (txn, table);
	}
	else
	{
		// Use RocksDB's estimate for fast approximate counts
		uint64_t count = 0;
		db->GetIntProperty (table_to_column_family (table), "rocksdb.estimate-num-keys", &count);
		return count;
	}
}

bool backend_rocksdb::count_is_exact (nano::store::table table) const
{
	switch (table)
	{
		case nano::store::table::pruned:
		case nano::store::table::final_votes:
			// These tables use rocksdb.estimate-num-keys which may be inaccurate
			return false;
		case nano::store::table::accounts:
		case nano::store::table::blocks:
		case nano::store::table::confirmation_height:
		case nano::store::table::default_unused:
		case nano::store::table::meta:
		case nano::store::table::online_weight:
		case nano::store::table::peers:
		case nano::store::table::pending:
		case nano::store::table::vote:
		case nano::store::table::rep_weights:
		case nano::store::table::successor:
		case nano::store::table::unchecked:
		case nano::store::table::frontiers:
			// These tables use iteration for exact counts (may be slow)
			return true;
	}
	return false;
}

// This function manages its own transaction rather than accepting one from the caller.
// RocksDB's WriteBatchWithIndex does not support DeleteRange - reads within the same
// transaction cannot see range tombstones. By managing our own transaction and committing
// it before returning, subsequent reads will correctly see the cleared table.
int backend_rocksdb::clear (nano::store::table table)
{
	auto txn = tx_begin_write ();

	auto * cf = table_to_column_family (table);
	auto * rtxn = std::get<::rocksdb::Transaction *> (rocksdb::tx (txn));

	::rocksdb::ReadOptions ro;
	std::unique_ptr<::rocksdb::Iterator> it (rtxn->GetIterator (ro, cf));

	it->SeekToFirst ();
	if (!it->Valid ())
	{
		return ::rocksdb::Status::kOk; // Table is already empty
	}

	const std::string first_key = it->key ().ToString ();

	it->SeekToLast ();
	if (!it->Valid ())
	{
		return ::rocksdb::Status::kOk; // Defensive check, should not happen
	}

	std::string end_key = it->key ().ToString ();
	end_key.push_back ('\0'); // Make end strictly greater than last_key (exclusive end)

	// Add a range tombstone to the transaction's write batch
	auto * wbwi = rtxn->GetWriteBatch (); // WriteBatchWithIndex*
	auto * wb = wbwi->GetWriteBatch (); // WriteBatch*
	auto status = wb->DeleteRange (cf, first_key, ::rocksdb::Slice (end_key));
	release_assert (status.ok (), "delete range failed", status.ToString ());

	return ::rocksdb::Status::kOk;
}

bool backend_rocksdb::drop_table (std::string const & name)
{
	if (!column_family_exists (name))
	{
		return false; // Table doesn't exist
	}

	auto const handle = get_column_family (name);

	auto status1 = db->DropColumnFamily (handle);
	release_assert (success (status1.code ()), error_string (status1.code ()));

	auto status2 = db->DestroyColumnFamilyHandle (handle);
	release_assert (success (status2.code ()), error_string (status2.code ()));

	// Remove from handles vector
	std::erase_if (handles, [handle] (auto & h) {
		if (h.get () == handle)
		{
			// The handle resource is deleted by RocksDB
			[[maybe_unused]] auto ptr = h.release ();
			return true;
		}
		return false;
	});

	// Remove from table_handles if it was tracked
	std::erase_if (table_handles, [handle] (auto const & pair) {
		return pair.second == handle;
	});

	return true;
}

bool backend_rocksdb::table_exists (std::string const & name) const
{
	return column_family_exists (name);
}

nano::store::iterator backend_rocksdb::begin (nano::store::transaction const & txn, nano::store::table table) const
{
	return nano::store::iterator{ txn, iterator::begin (db.get (), rocksdb::tx (txn), table_to_column_family (table)) };
}

nano::store::iterator backend_rocksdb::begin (nano::store::transaction const & txn, nano::store::table table, nano::store::db_val const & key) const
{
	auto key_slice = to_slice (key);
	return nano::store::iterator{ txn, iterator::lower_bound (db.get (), rocksdb::tx (txn), table_to_column_family (table), key_slice) };
}

nano::store::iterator backend_rocksdb::end (nano::store::transaction const & txn, nano::store::table table) const
{
	return nano::store::iterator{ txn, iterator::end (db.get (), rocksdb::tx (txn), table_to_column_family (table)) };
}

bool backend_rocksdb::success (int status) const
{
	return static_cast<int> (::rocksdb::Status::Code::kOk) == status;
}

bool backend_rocksdb::not_found (int status) const
{
	return static_cast<int> (::rocksdb::Status::Code::kNotFound) == status;
}

std::string backend_rocksdb::error_string (int status) const
{
	return "status: " + std::to_string (status);
}

nano::store::read_transaction backend_rocksdb::tx_begin_read () const
{
	return store::read_transaction{ std::make_unique<nano::store::rocksdb::read_transaction_impl> (db.get (), txn_tracking_callbacks ()) };
}

nano::store::write_transaction backend_rocksdb::tx_begin_write ()
{
	release_assert (transaction_db != nullptr);
	return store::write_transaction{ std::make_unique<nano::store::rocksdb::write_transaction_impl> (transaction_db, txn_tracking_callbacks ()) };
}

void backend_rocksdb::backup ()
{
	release_assert (db != nullptr, "database must be open to perform backup");

	std::unique_ptr<::rocksdb::BackupEngine> backup_engine;
	::rocksdb::BackupEngine * backup_engine_raw;
	auto backup_path = database_path.parent_path () / "backup";
	::rocksdb::BackupEngineOptions backup_options (backup_path.string ());
	backup_options.share_table_files = true;
	backup_options.max_background_operations = std::thread::hardware_concurrency ();

	auto status = ::rocksdb::BackupEngine::Open (::rocksdb::Env::Default (), backup_options, &backup_engine_raw);
	backup_engine.reset (backup_engine_raw);
	if (!status.ok ())
	{
		throw std::runtime_error ("Failed to open backup engine: " + status.ToString ());
	}

	status = backup_engine->CreateNewBackup (db.get ());
	if (!status.ok ())
	{
		throw std::runtime_error ("Failed to create backup: " + status.ToString ());
	}
}

void backend_rocksdb::copy_with_compaction (std::filesystem::path const & destination_path)
{
	std::unique_ptr<::rocksdb::BackupEngine> backup_engine;
	{
		::rocksdb::BackupEngine * backup_engine_raw;
		::rocksdb::BackupEngineOptions backup_options (destination_path.string ());
		// Use incremental backups (default)
		backup_options.share_table_files = true;
		// Increase number of threads used for copying
		backup_options.max_background_operations = std::thread::hardware_concurrency ();
		auto status = ::rocksdb::BackupEngine::Open (::rocksdb::Env::Default (), backup_options, &backup_engine_raw);
		backup_engine.reset (backup_engine_raw);
		if (!status.ok ())
		{
			throw std::runtime_error ("Failed to open backup engine: " + status.ToString ());
		}
	}

	auto status = backup_engine->CreateNewBackup (db.get ());
	if (!status.ok ())
	{
		throw std::runtime_error ("Failed to create backup: " + status.ToString ());
	}

	std::vector<::rocksdb::BackupInfo> backup_infos;
	backup_engine->GetBackupInfo (&backup_infos);

	for (auto const & backup_info : backup_infos)
	{
		status = backup_engine->VerifyBackup (backup_info.backup_id);
		if (!status.ok ())
		{
			throw std::runtime_error ("Failed to verify backup: " + status.ToString ());
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
			throw std::runtime_error ("Failed to open backup engine for restore: " + status.ToString ());
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

	if (!status.ok ())
	{
		throw std::runtime_error ("Failed to restore database from backup: " + status.ToString ());
	}

	// Open it so that it flushes all WAL files
	backend_rocksdb temp_backend (destination_path, config, nano::default_logger ());
	// Opening a database causes WAL to be flushed
}

std::string backend_rocksdb::get_vendor () const
{
	return boost::str (boost::format ("RocksDB %1%.%2%.%3%") % ROCKSDB_MAJOR % ROCKSDB_MINOR % ROCKSDB_PATCH);
}

std::string backend_rocksdb::get_database_path () const
{
	return database_path.string ();
}

void backend_rocksdb::collect_memory_stats (boost::property_tree::ptree & ptree) const
{
	uint64_t val = 0;

	// Approximate size of active and unflushed immutable memtables (bytes)
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kCurSizeAllMemTables, &val);
	ptree.put ("cur_size_all_mem_tables", val);

	// Approximate size of active, unflushed immutable, and pinned immutable memtables (bytes)
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kSizeAllMemTables, &val);
	ptree.put ("size_all_mem_tables", val);

	// Estimated memory used for reading SST tables, excluding memory used in block cache (e.g. filter and index blocks)
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateTableReadersMem, &val);
	ptree.put ("estimate_table_readers_mem", val);

	// An estimate of the amount of live data in bytes
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateLiveDataSize, &val);
	ptree.put ("estimate_live_data_size", val);

	// Returns 1 if at least one compaction is pending; otherwise, returns 0
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kCompactionPending, &val);
	ptree.put ("compaction_pending", val);

	// Estimated number of total keys in the active and unflushed immutable memtables and storage
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimateNumKeys, &val);
	ptree.put ("estimate_num_keys", val);

	// Estimated total number of bytes compaction needs to rewrite to get all levels down to under target size
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kEstimatePendingCompactionBytes, &val);
	ptree.put ("estimate_pending_compaction_bytes", val);

	// Total size (bytes) of all SST files (WARNING: may slow down online queries if there are too many files)
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kTotalSstFilesSize, &val);
	ptree.put ("total_sst_files_size", val);

	// Block cache capacity
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBlockCacheCapacity, &val);
	ptree.put ("block_cache_capacity", val);

	// Memory size for the entries residing in block cache
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBlockCacheUsage, &val);
	ptree.put ("block_cache_usage", val);

	// Memory size for the entries pinned in block cache
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBlockCachePinnedUsage, &val);
	ptree.put ("block_cache_pinned_usage", val);

	// Returns 1 if a memtable flush is pending; otherwise, returns 0
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kMemTableFlushPending, &val);
	ptree.put ("mem_table_flush_pending", val);

	// Number of currently running flushes
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumRunningFlushes, &val);
	ptree.put ("num_running_flushes", val);

	// Number of currently running compactions
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumRunningCompactions, &val);
	ptree.put ("num_running_compactions", val);

	// Returns 1 if writes are currently stopped; otherwise, returns 0
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kIsWriteStopped, &val);
	ptree.put ("is_write_stopped", val);

	// Current delayed write rate (bytes/sec); 0 means no delay
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kActualDelayedWriteRate, &val);
	ptree.put ("actual_delayed_write_rate", val);

	// Total size (bytes) of all live SST files
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kLiveSstFilesSize, &val);
	ptree.put ("live_sst_files_size", val);

	// Base level for LSM compaction
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBaseLevel, &val);
	ptree.put ("base_level", val);

	// Number of immutable memtables not yet flushed
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumImmutableMemTable, &val);
	ptree.put ("num_immutable_mem_table", val);

	// Number of immutable memtables that have been flushed
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumImmutableMemTableFlushed, &val);
	ptree.put ("num_immutable_mem_table_flushed", val);

	// Number of entries in the active memtable
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumEntriesActiveMemTable, &val);
	ptree.put ("num_entries_active_mem_table", val);

	// Number of entries in immutable memtables
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumEntriesImmMemTables, &val);
	ptree.put ("num_entries_imm_mem_tables", val);

	// Number of delete entries in the active memtable
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumDeletesActiveMemTable, &val);
	ptree.put ("num_deletes_active_mem_table", val);

	// Number of delete entries in immutable memtables
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumDeletesImmMemTables, &val);
	ptree.put ("num_deletes_imm_mem_tables", val);

	// Number of unreleased snapshots
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumSnapshots, &val);
	ptree.put ("num_snapshots", val);

	// Unix timestamp of the oldest unreleased snapshot
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kOldestSnapshotTime, &val);
	ptree.put ("oldest_snapshot_time", val);

	// Number of live versions
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kNumLiveVersions, &val);
	ptree.put ("num_live_versions", val);

	// Number of background errors
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kBackgroundErrors, &val);
	ptree.put ("background_errors", val);

	// Returns 1 if obsolete file deletions are enabled; otherwise, returns 0
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kIsFileDeletionsEnabled, &val);
	ptree.put ("is_file_deletions_enabled", val);

	// Minimum log number to keep
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kMinLogNumberToKeep, &val);
	ptree.put ("min_log_number_to_keep", val);

	// Minimum obsolete SST number to keep
	db->GetAggregatedIntProperty (::rocksdb::DB::Properties::kMinObsoleteSstNumberToKeep, &val);
	ptree.put ("min_obsolete_sst_number_to_keep", val);

	// Per-level file counts
	boost::property_tree::ptree levels;
	for (int level = 0; level < 7; ++level)
	{
		std::string property = ::rocksdb::DB::Properties::kNumFilesAtLevelPrefix + std::to_string (level);
		db->GetAggregatedIntProperty (property, &val);
		levels.put ("l" + std::to_string (level) + "_num_files", val);
	}
	ptree.add_child ("levels", levels);

	// Per-column family stats
	boost::property_tree::ptree cf_stats;
	for (auto const & [table, handle] : table_handles)
	{
		boost::property_tree::ptree cf_info;
		uint64_t cf_memtable = 0;
		uint64_t cf_num_keys = 0;

		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kCurSizeActiveMemTable, &cf_memtable);
		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kEstimateNumKeys, &cf_num_keys);

		cf_info.put ("memtable_size", cf_memtable);
		cf_info.put ("estimate_num_keys", cf_num_keys);

		// Additional per-column family stats
		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kNumImmutableMemTable, &val);
		cf_info.put ("num_immutable_mem_table", val);

		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kNumEntriesActiveMemTable, &val);
		cf_info.put ("num_entries_active_mem_table", val);

		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kNumEntriesImmMemTables, &val);
		cf_info.put ("num_entries_imm_mem_tables", val);

		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kNumDeletesActiveMemTable, &val);
		cf_info.put ("num_deletes_active_mem_table", val);

		db->GetIntProperty (handle, ::rocksdb::DB::Properties::kNumDeletesImmMemTables, &val);
		cf_info.put ("num_deletes_imm_mem_tables", val);

		cf_stats.add_child (handle->GetName (), cf_info);
	}
	ptree.add_child ("column_families", cf_stats);
}

void backend_rocksdb::generate_tombstone_map ()
{
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::store::table::blocks), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::store::table::accounts), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::store::table::pending), std::forward_as_tuple (0, 25000));
}

void backend_rocksdb::flush_tombstones_check (nano::store::table table)
{
	// Update the number of deletes for some tables, and force a flush if there are too many tombstones
	// as it can affect read performance.
	if (auto it = tombstone_map.find (table); it != tombstone_map.end ())
	{
		auto & tombstone_info = it->second;
		if (++tombstone_info.num_since_last_flush > tombstone_info.max)
		{
			tombstone_info.num_since_last_flush = 0;
			flush_table (table);
		}
	}
}

void backend_rocksdb::flush_table (nano::store::table table)
{
	db->Flush (::rocksdb::FlushOptions{}, table_to_column_family (table));
}

void backend_rocksdb::on_flush (::rocksdb::FlushJobInfo const & flush_info)
{
	// Reset appropriate tombstone counters
	if (auto it = name_to_table.find (flush_info.cf_name); it != name_to_table.end ())
	{
		if (auto tomb_it = tombstone_map.find (it->second); tomb_it != tombstone_map.end ())
		{
			tomb_it->second.num_since_last_flush = 0;
		}
	}
}

backend_rocksdb::tombstone_info::tombstone_info (uint64_t num, uint64_t max_a) :
	num_since_last_flush (num),
	max (max_a)
{
}
}
