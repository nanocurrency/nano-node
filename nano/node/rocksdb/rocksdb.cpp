#include <nano/lib/rocksdbconfig.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/node/rocksdb/rocksdb_iterator.hpp>
#include <nano/node/rocksdb/rocksdb_txn.hpp>

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

namespace nano
{
template <>
void * rocksdb_val::data () const
{
	return (void *)value.data ();
}

template <>
std::size_t rocksdb_val::size () const
{
	return value.size ();
}

template <>
rocksdb_val::db_val (std::size_t size_a, void * data_a) :
	value (static_cast<char const *> (data_a), size_a)
{
}

template <>
void rocksdb_val::convert_buffer_to_value ()
{
	value = ::rocksdb::Slice (reinterpret_cast<char const *> (buffer->data ()), buffer->size ());
}
}

nano::rocksdb::store::store (nano::logger_mt & logger_a, boost::filesystem::path const & path_a, nano::ledger_constants & constants, nano::rocksdb_config const & rocksdb_config_a, bool open_read_only_a) :
	// clang-format off
	nano::store{
		block_store,
		frontier_store,
		account_store,
		pending_store,
		unchecked_store,
		online_weight_store,
		pruned_store,
		peer_store,
		confirmation_height_store,
		final_vote_store,
		reverse_link_store,
		version_store
	},
	// clang-format on
	block_store{ *this },
	frontier_store{ *this },
	account_store{ *this },
	pending_store{ *this },
	unchecked_store{ *this },
	online_weight_store{ *this },
	pruned_store{ *this },
	peer_store{ *this },
	confirmation_height_store{ *this },
	final_vote_store{ *this },
	reverse_link_store{ *this },
	version_store{ *this },
	logger{ logger_a },
	constants{ constants },
	rocksdb_config{ rocksdb_config_a },
	max_block_write_batch_num_m{ nano::narrow_cast<unsigned> (blocks_memtable_size_bytes () / (2 * (sizeof (nano::block_type) + nano::state_block::size + nano::block_sideband::size (nano::block_type::state)))) },
	cf_name_table_map{ create_cf_name_table_map () }
{
	boost::system::error_code error_mkdir, error_chmod;
	boost::filesystem::create_directories (path_a, error_mkdir);
	nano::set_secure_perm_directory (path_a, error_chmod);
	error = static_cast<bool> (error_mkdir);

	if (!error)
	{
		generate_tombstone_map ();
		small_table_factory.reset (::rocksdb::NewBlockBasedTableFactory (get_small_table_options ()));
		if (!open_read_only_a)
		{
			construct_column_family_mutexes ();
		}
		open (error, path_a, open_read_only_a);
	}
}

std::unordered_map<char const *, nano::tables> nano::rocksdb::store::create_cf_name_table_map () const
{
	std::unordered_map<char const *, nano::tables> map{ { ::rocksdb::kDefaultColumnFamilyName.c_str (), tables::default_unused },
		{ "frontiers", tables::frontiers },
		{ "accounts", tables::accounts },
		{ "blocks", tables::blocks },
		{ "pending", tables::pending },
		{ "unchecked", tables::unchecked },
		{ "vote", tables::vote },
		{ "online_weight", tables::online_weight },
		{ "meta", tables::meta },
		{ "peers", tables::peers },
		{ "confirmation_height", tables::confirmation_height },
		{ "pruned", tables::pruned },
		{ "final_votes", tables::final_votes },
		{ "reverse_links", tables::reverse_links } };

	debug_assert (map.size () == all_tables ().size () + 1);
	return map;
}

void nano::rocksdb::store::open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a)
{
	auto column_families = create_column_families ();
	auto options = get_db_options ();
	::rocksdb::Status s;

	std::vector<::rocksdb::ColumnFamilyHandle *> handles_l;
	if (open_read_only_a)
	{
		::rocksdb::DB * db_l;
		s = ::rocksdb::DB::OpenForReadOnly (options, path_a.string (), column_families, &handles_l, &db_l);
		db.reset (db_l);
	}
	else
	{
		s = ::rocksdb::OptimisticTransactionDB::Open (options, path_a.string (), column_families, &handles_l, &optimistic_db);
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

	if (!error_a)
	{
		auto transaction = tx_begin_read ();
		auto version_l = version.get (transaction);
		if (version_l > version_current)
		{
			error_a = true;
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
		}
	}
}

void nano::rocksdb::store::generate_tombstone_map ()
{
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::unchecked), std::forward_as_tuple (0, 50000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::blocks), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::accounts), std::forward_as_tuple (0, 25000));
	tombstone_map.emplace (std::piecewise_construct, std::forward_as_tuple (nano::tables::pending), std::forward_as_tuple (0, 25000));
}

rocksdb::ColumnFamilyOptions nano::rocksdb::store::get_common_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const
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

rocksdb::ColumnFamilyOptions nano::rocksdb::store::get_cf_options (std::string const & cf_name_a) const
{
	::rocksdb::ColumnFamilyOptions cf_options;
	auto const memtable_size_bytes = base_memtable_size_bytes ();
	auto const block_cache_size_bytes = 1024ULL * 1024 * rocksdb_config.memory_multiplier * base_block_cache_size;
	if (cf_name_a == "unchecked")
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_active_table_options (block_cache_size_bytes * 4)));
		cf_options = get_active_cf_options (table_factory, memtable_size_bytes);

		// Create prefix bloom for memtable with the size of write_buffer_size * memtable_prefix_bloom_size_ratio
		cf_options.memtable_prefix_bloom_size_ratio = 0.25;

		// Number of files in level 0 which triggers compaction. Size of L0 and L1 should be kept similar as this is the only compaction which is single threaded
		cf_options.level0_file_num_compaction_trigger = 2;

		// L1 size, compaction is triggered for L0 at this size (2 SST files in L1)
		cf_options.max_bytes_for_level_base = memtable_size_bytes * 2;
	}
	else if (cf_name_a == "blocks")
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
	else if (cf_name_a == "reverse_links")
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

std::vector<rocksdb::ColumnFamilyDescriptor> nano::rocksdb::store::create_column_families ()
{
	std::vector<::rocksdb::ColumnFamilyDescriptor> column_families;
	for (auto & [cf_name, table] : cf_name_table_map)
	{
		(void)table;
		column_families.emplace_back (cf_name, get_cf_options (cf_name));
	}
	return column_families;
}

nano::write_transaction nano::rocksdb::store::tx_begin_write (std::vector<nano::tables> const & tables_requiring_locks_a, std::vector<nano::tables> const & tables_no_locks_a)
{
	std::unique_ptr<nano::write_rocksdb_txn> txn;
	release_assert (optimistic_db != nullptr);
	if (tables_requiring_locks_a.empty () && tables_no_locks_a.empty ())
	{
		// Use all tables if none are specified
		txn = std::make_unique<nano::write_rocksdb_txn> (optimistic_db, all_tables (), tables_no_locks_a, write_lock_mutexes);
	}
	else
	{
		txn = std::make_unique<nano::write_rocksdb_txn> (optimistic_db, tables_requiring_locks_a, tables_no_locks_a, write_lock_mutexes);
	}

	// Tables must be kept in alphabetical order. These can be used for mutex locking, so order is important to prevent deadlocking
	debug_assert (std::is_sorted (tables_requiring_locks_a.begin (), tables_requiring_locks_a.end ()));

	return nano::write_transaction{ std::move (txn) };
}

nano::read_transaction nano::rocksdb::store::tx_begin_read () const
{
	return nano::read_transaction{ std::make_unique<nano::read_rocksdb_txn> (db.get ()) };
}

std::string nano::rocksdb::store::vendor_get () const
{
	return boost::str (boost::format ("RocksDB %1%.%2%.%3%") % ROCKSDB_MAJOR % ROCKSDB_MINOR % ROCKSDB_PATCH);
}

rocksdb::ColumnFamilyHandle * nano::rocksdb::store::table_to_column_family (tables table_a) const
{
	auto & handles_l = handles;
	auto get_handle = [&handles_l] (char const * name) {
		auto iter = std::find_if (handles_l.begin (), handles_l.end (), [name] (auto & handle) {
			return (handle->GetName () == name);
		});
		debug_assert (iter != handles_l.end ());
		return (*iter).get ();
	};

	switch (table_a)
	{
		case tables::frontiers:
			return get_handle ("frontiers");
		case tables::accounts:
			return get_handle ("accounts");
		case tables::blocks:
			return get_handle ("blocks");
		case tables::pending:
			return get_handle ("pending");
		case tables::unchecked:
			return get_handle ("unchecked");
		case tables::vote:
			return get_handle ("vote");
		case tables::online_weight:
			return get_handle ("online_weight");
		case tables::meta:
			return get_handle ("meta");
		case tables::peers:
			return get_handle ("peers");
		case tables::pruned:
			return get_handle ("pruned");
		case tables::confirmation_height:
			return get_handle ("confirmation_height");
		case tables::final_votes:
			return get_handle ("final_votes");
		case tables::reverse_links:
			return get_handle ("reverse_links");
		default:
			release_assert (false);
			return get_handle ("");
	}
}

bool nano::rocksdb::store::exists (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a) const
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

int nano::rocksdb::store::del (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a)
{
	debug_assert (transaction_a.contains (table_a));
	// RocksDB does not report not_found status, it is a pre-condition that the key exists
	debug_assert (exists (transaction_a, table_a, key_a));
	flush_tombstones_check (table_a);
	return tx (transaction_a)->Delete (table_to_column_family (table_a), key_a).code ();
}

void nano::rocksdb::store::flush_tombstones_check (tables table_a)
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

void nano::rocksdb::store::flush_table (nano::tables table_a)
{
	db->Flush (::rocksdb::FlushOptions{}, table_to_column_family (table_a));
}

rocksdb::Transaction * nano::rocksdb::store::tx (nano::transaction const & transaction_a) const
{
	debug_assert (!is_read (transaction_a));
	return static_cast<::rocksdb::Transaction *> (transaction_a.get_handle ());
}

int nano::rocksdb::store::get (nano::transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val & value_a) const
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

int nano::rocksdb::store::put (nano::write_transaction const & transaction_a, tables table_a, nano::rocksdb_val const & key_a, nano::rocksdb_val const & value_a)
{
	debug_assert (transaction_a.contains (table_a));
	auto txn = tx (transaction_a);
	return txn->Put (table_to_column_family (table_a), key_a, value_a).code ();
}

bool nano::rocksdb::store::not_found (int status) const
{
	return (status_code_not_found () == status);
}

bool nano::rocksdb::store::success (int status) const
{
	return (static_cast<int> (::rocksdb::Status::Code::kOk) == status);
}

int nano::rocksdb::store::status_code_not_found () const
{
	return static_cast<int> (::rocksdb::Status::Code::kNotFound);
}

uint64_t nano::rocksdb::store::count (nano::transaction const & transaction_a, tables table_a) const
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
	// This is only an estimation
	else if (table_a == tables::unchecked)
	{
		db->GetIntProperty (table_to_column_family (table_a), "rocksdb.estimate-num-keys", &sum);
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
	else if (table_a == tables::reverse_links)
	{
		for (auto i (reverse_link.begin (transaction_a)), n (reverse_link.end ()); i != n; ++i)
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

int nano::rocksdb::store::drop (nano::write_transaction const & transaction_a, tables table_a)
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
				status = del (transaction_a, tables::peers, nano::rocksdb_val (i->first));
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

int nano::rocksdb::store::clear (::rocksdb::ColumnFamilyHandle * column_family)
{
	// Dropping completely removes the column
	auto name = column_family->GetName ();
	auto status = db->DropColumnFamily (column_family);
	release_assert (status.ok ());

	// Need to add it back as we just want to clear the contents
	auto handle_it = std::find_if (handles.begin (), handles.end (), [column_family] (auto & handle) {
		return handle.get () == column_family;
	});
	debug_assert (handle_it != handles.cend ());
	status = db->CreateColumnFamily (get_cf_options (name), name, &column_family);
	release_assert (status.ok ());
	handle_it->reset (column_family);
	return status.code ();
}

void nano::rocksdb::store::construct_column_family_mutexes ()
{
	for (auto table : all_tables ())
	{
		write_lock_mutexes.emplace (std::piecewise_construct, std::forward_as_tuple (table), std::forward_as_tuple ());
	}
}

rocksdb::Options nano::rocksdb::store::get_db_options ()
{
	::rocksdb::Options db_options;
	db_options.create_if_missing = true;
	db_options.create_missing_column_families = true;

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

	auto event_listener_l = new event_listener ([this] (::rocksdb::FlushJobInfo const & flush_job_info_a) { this->on_flush (flush_job_info_a); });
	db_options.listeners.emplace_back (event_listener_l);

	return db_options;
}

rocksdb::BlockBasedTableOptions nano::rocksdb::store::get_active_table_options (std::size_t lru_size) const
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

rocksdb::BlockBasedTableOptions nano::rocksdb::store::get_small_table_options () const
{
	::rocksdb::BlockBasedTableOptions table_options;
	// Improve point lookup performance be using the data block hash index (uses about 5% more space).
	table_options.data_block_index_type = ::rocksdb::BlockBasedTableOptions::DataBlockIndexType::kDataBlockBinaryAndHash;
	table_options.data_block_hash_table_util_ratio = 0.75;
	table_options.block_size = 1024ULL;
	return table_options;
}

rocksdb::ColumnFamilyOptions nano::rocksdb::store::get_small_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a) const
{
	auto const memtable_size_bytes = 10000;
	auto cf_options = get_common_cf_options (table_factory_a, memtable_size_bytes);

	// Number of files in level 0 which triggers compaction. Size of L0 and L1 should be kept similar as this is the only compaction which is single threaded
	cf_options.level0_file_num_compaction_trigger = 1;

	// L1 size, compaction is triggered for L0 at this size (1 SST file in L1)
	cf_options.max_bytes_for_level_base = memtable_size_bytes;

	return cf_options;
}

::rocksdb::ColumnFamilyOptions nano::rocksdb::store::get_active_cf_options (std::shared_ptr<::rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const
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

void nano::rocksdb::store::on_flush (::rocksdb::FlushJobInfo const & flush_job_info_a)
{
	// Reset appropriate tombstone counters
	if (auto it = tombstone_map.find (cf_name_table_map[flush_job_info_a.cf_name.c_str ()]); it != tombstone_map.end ())
	{
		it->second.num_since_last_flush = 0;
	}
}

std::vector<nano::tables> nano::rocksdb::store::all_tables () const
{
	return std::vector<nano::tables>{ tables::accounts, tables::blocks, tables::confirmation_height, tables::final_votes, tables::frontiers, tables::meta, tables::online_weight, tables::peers, tables::pending, tables::pruned, tables::reverse_links, tables::unchecked, tables::vote };
}

bool nano::rocksdb::store::copy_db (boost::filesystem::path const & destination_path)
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
		for (auto const & path : boost::make_iterator_range (boost::filesystem::directory_iterator (destination_path)))
		{
			if (boost::filesystem::is_regular_file (path))
			{
				boost::filesystem::remove (path);
			}
		}

		// Now generate the relevant files from the backup
		status = backup_engine->RestoreDBFromLatestBackup (destination_path.string (), destination_path.string ());
	}

	// Open it so that it flushes all WAL files
	if (status.ok ())
	{
		nano::rocksdb::store rocksdb_store{ logger, destination_path.string (), constants, rocksdb_config, false };
		return !rocksdb_store.init_error ();
	}
	return false;
}

void nano::rocksdb::store::rebuild_db (nano::write_transaction const & transaction_a)
{
	// Not available for RocksDB
}

bool nano::rocksdb::store::init_error () const
{
	return error;
}

void nano::rocksdb::store::serialize_memory_stats (boost::property_tree::ptree & json)
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

unsigned long long nano::rocksdb::store::blocks_memtable_size_bytes () const
{
	return base_memtable_size_bytes ();
}

unsigned long long nano::rocksdb::store::base_memtable_size_bytes () const
{
	return 1024ULL * 1024 * rocksdb_config.memory_multiplier * base_memtable_size;
}

// This is a ratio of the blocks memtable size to keep total write transaction commit size down.
unsigned nano::rocksdb::store::max_block_write_batch_num () const
{
	return max_block_write_batch_num_m;
}

std::string nano::rocksdb::store::error_string (int status) const
{
	return std::to_string (status);
}

nano::rocksdb::store::tombstone_info::tombstone_info (uint64_t num_since_last_flush_a, uint64_t const max_a) :
	num_since_last_flush (num_since_last_flush_a),
	max (max_a)
{
}
