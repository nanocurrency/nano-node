#include <nano/lib/blocks.hpp>
#include <nano/lib/rocksdbconfig.hpp>
#include <nano/store/rocksdb/iterator.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>
#include <nano/store/rocksdb/utility.hpp>
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
	logger{ logger_a },
	constants{ constants },
	rocksdb_config{ rocksdb_config_a },
	max_block_write_batch_num_m{ nano::narrow_cast<unsigned> ((rocksdb_config_a.write_cache * 1024 * 1024) / (2 * (sizeof (nano::block_type) + nano::state_block::size + nano::block_sideband::size (nano::block_type::state)))) },
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
		s = ::rocksdb::TransactionDB::Open (options_a, ::rocksdb::TransactionDBOptions{}, path_a.string (), column_families, &handles_l, &transaction_db);
		if (transaction_db)
		{
			db.reset (transaction_db);
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

bool nano::store::rocksdb::component::do_upgrades (store::write_transaction & transaction)
{
	bool error_l{ false };
	auto version_l = version.get (transaction);
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
			logger.critical (nano::log::type::rocksdb, "The version of the ledger ({}) is too high for this node", version_l);
			error_l = true;
			break;
	}
	return error_l;
}

void nano::store::rocksdb::component::upgrade_v21_to_v22 (store::write_transaction & transaction)
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

	version.put (transaction, 22);

	logger.info (nano::log::type::rocksdb, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void nano::store::rocksdb::component::upgrade_v22_to_v23 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::rocksdb, "Upgrading database from v22 to v23...");

	if (column_family_exists ("rep_weights"))
	{
		logger.info (nano::log::type::rocksdb, "Dropping existing rep_weights table");
		auto const rep_weights_handle = get_column_family ("rep_weights");
		db->DropColumnFamily (rep_weights_handle);
		db->DestroyColumnFamilyHandle (rep_weights_handle);
		std::erase_if (handles, [rep_weights_handle] (auto & handle) {
			if (handle.get () == rep_weights_handle)
			{
				// The handle resource is deleted by RocksDB.
				[[maybe_unused]] auto ptr = handle.release ();
				return true;
			}
			return false;
		});
		transaction.refresh ();
	}

	{
		logger.info (nano::log::type::rocksdb, "Creating table rep_weights");
		::rocksdb::ColumnFamilyOptions new_cf_options;
		::rocksdb::ColumnFamilyHandle * new_cf_handle;
		::rocksdb::Status status = db->CreateColumnFamily (new_cf_options, "rep_weights", &new_cf_handle);
		release_assert (success (status.code ()));
		handles.emplace_back (new_cf_handle);
		transaction.refresh ();
	}

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
			nano::store::rocksdb::db_val value;
			auto status = get (transaction, table_to_column_family (tables::rep_weights), account_info.representative, value);
			if (success (status))
			{
				total = nano::amount{ value }.number ();
			}
			total += account_info.balance.number ();
			status = put (transaction, table_to_column_family (tables::rep_weights), account_info.representative, nano::amount{ total });
			release_assert_success (status);
		}

		processed++;
		if (processed % batch_size == 0)
		{
			logger.info (nano::log::type::rocksdb, "Processed {} accounts", processed);
			transaction.refresh (); // Refresh to prevent excessive memory usage
		}
	});

	logger.info (nano::log::type::rocksdb, "Done processing {} accounts", processed);
	version.put (transaction, 23);

	logger.info (nano::log::type::rocksdb, "Upgrading database from v22 to v23 completed");
}

void nano::store::rocksdb::component::upgrade_v23_to_v24 (store::write_transaction & transaction)
{
	logger.info (nano::log::type::rocksdb, "Upgrading database from v23 to v24...");

	if (column_family_exists ("frontiers"))
	{
		auto const frontiers_handle = get_column_family ("frontiers");
		db->DropColumnFamily (frontiers_handle);
		db->DestroyColumnFamilyHandle (frontiers_handle);
		std::erase_if (handles, [frontiers_handle] (auto & handle) {
			if (handle.get () == frontiers_handle)
			{
				// The handle resource is deleted by RocksDB.
				[[maybe_unused]] auto ptr = handle.release ();
				return true;
			}
			return false;
		});
		logger.debug (nano::log::type::rocksdb, "Finished removing frontiers table");
	}

	version.put (transaction, 24);
	logger.info (nano::log::type::rocksdb, "Upgrading database from v23 to v24 completed");
}

rocksdb::ColumnFamilyOptions nano::store::rocksdb::component::get_cf_options (std::string const & cf_name_a) const
{
	::rocksdb::ColumnFamilyOptions cf_options;
	if (cf_name_a != ::rocksdb::kDefaultColumnFamilyName)
	{
		std::shared_ptr<::rocksdb::TableFactory> table_factory (::rocksdb::NewBlockBasedTableFactory (get_table_options ()));
		cf_options.table_factory = table_factory;
		// Size of each memtable (write buffer for this column family)
		cf_options.write_buffer_size = rocksdb_config.write_cache * 1024 * 1024;
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

nano::store::write_transaction nano::store::rocksdb::component::tx_begin_write ()
{
	release_assert (transaction_db != nullptr);
	return store::write_transaction{ std::make_unique<nano::store::rocksdb::write_transaction_impl> (transaction_db) };
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

bool nano::store::rocksdb::component::exists (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a) const
{
	::rocksdb::PinnableSlice slice;
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db->Get (snapshot_options (transaction_a), table_a, key_a, &slice);
	}
	else
	{
		::rocksdb::ReadOptions options;
		options.fill_cache = false;
		status = rocksdb::tx (transaction_a)->Get (options, table_a, key_a, &slice);
	}

	return (status.ok ());
}

int nano::store::rocksdb::component::del (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a)
{
	// RocksDB does not report not_found status, it is a pre-condition that the key exists
	debug_assert (exists (transaction_a, table_a, key_a));
	return tx (transaction_a)->Delete (table_a, key_a).code ();
}

int nano::store::rocksdb::component::get (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val & value_a) const
{
	::rocksdb::ReadOptions options;
	::rocksdb::PinnableSlice slice;
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db->Get (snapshot_options (transaction_a), table_a, key_a, &slice);
	}
	else
	{
		status = tx (transaction_a)->Get (options, table_a, key_a, &slice);
	}

	if (status.ok ())
	{
		value_a.buffer = std::make_shared<std::vector<uint8_t>> (slice.size ());
		std::memcpy (value_a.buffer->data (), slice.data (), slice.size ());
		value_a.convert_buffer_to_value ();
	}
	return status.code ();
}

int nano::store::rocksdb::component::put (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val const & value_a)
{
	auto txn = tx (transaction_a);
	return txn->Put (table_a, key_a, value_a).code ();
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
				status = del (transaction_a, table_to_column_family (tables::peers), nano::store::rocksdb::db_val (i->first));
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

rocksdb::Options nano::store::rocksdb::component::get_db_options ()
{
	::rocksdb::Options db_options;
	db_options.create_if_missing = true;
	db_options.create_missing_column_families = true;

	// Optimize RocksDB. This is the easiest way to get RocksDB to perform well
	db_options.OptimizeLevelStyleCompaction ();

	// Set max number of threads
	db_options.IncreaseParallelism (rocksdb_config.io_threads);

	// Not compressing any SST files for compatibility reasons.
	db_options.compression = ::rocksdb::kNoCompression;

	return db_options;
}

rocksdb::BlockBasedTableOptions nano::store::rocksdb::component::get_table_options () const
{
	::rocksdb::BlockBasedTableOptions table_options;

	// Improve point lookup performance be using the data block hash index (uses about 5% more space).
	table_options.data_block_index_type = ::rocksdb::BlockBasedTableOptions::DataBlockIndexType::kDataBlockBinaryAndHash;

	// Using storage format_version 5.
	// Version 5 offers improved read spead, caching and better compression (if enabled)
	// Any existing ledger data in version 4 will not be migrated. New data will be written in version 5.
	table_options.format_version = 5;

	// Block cache for reads
	table_options.block_cache = ::rocksdb::NewLRUCache (rocksdb_config.read_cache * 1024 * 1024);

	// Bloom filter to help with point reads. 10bits gives 1% false positive rate.
	table_options.filter_policy.reset (::rocksdb::NewBloomFilterPolicy (10, false));

	return table_options;
}

std::vector<nano::tables> nano::store::rocksdb::component::all_tables () const
{
	return std::vector<nano::tables>{ tables::accounts, tables::blocks, tables::confirmation_height, tables::final_votes, tables::meta, tables::online_weight, tables::peers, tables::pending, tables::pruned, tables::vote, tables::rep_weights };
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

// This is a ratio of the blocks memtable size to keep total write transaction commit size down.
unsigned nano::store::rocksdb::component::max_block_write_batch_num () const
{
	return max_block_write_batch_num_m;
}

std::string nano::store::rocksdb::component::error_string (int status) const
{
	return std::to_string (status);
}
