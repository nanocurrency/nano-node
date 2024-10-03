#include <nano/store/rocksdb/utility.hpp>
#include <nano/store/rocksdb/transaction_impl.hpp>

#include <rocksdb/iterator.h>

auto nano::store::rocksdb::count (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a) -> uint64_t
{
	uint64_t count = 0;
	std::unique_ptr<::rocksdb::Iterator> it{ iter (transaction_a, table_a) };
	for (it->SeekToFirst (); it->Valid (); it->Next ())
	{
		count++;
	}
	release_assert (it->status ().ok ());
	
	return count;
}

auto nano::store::rocksdb::db (store::transaction const & transaction_a) -> ::rocksdb::DB *
{
	if (is_read (transaction_a))
	{
		return static_cast<std::pair<::rocksdb::DB *, ::rocksdb::ReadOptions> *> (transaction_a.get_handle ())->first;
	}
	return static_cast<std::pair<::rocksdb::TransactionDB *, ::rocksdb::Transaction *> *> (transaction_a.get_handle ())->first;
}

int nano::store::rocksdb::del (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a)
{
	// RocksDB does not report not_found status, it is a pre-condition that the key exists
	debug_assert (exists (transaction_a, table_a, key_a));
	return tx (transaction_a)->Delete (table_a, key_a).code ();
}

bool nano::store::rocksdb::exists (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a)
{
	::rocksdb::PinnableSlice slice;
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db (transaction_a)->Get (snapshot_options (transaction_a), table_a, key_a, &slice);
	}
	else
	{
		::rocksdb::ReadOptions options;
		options.fill_cache = false;
		status = tx (transaction_a)->Get (options, table_a, key_a, &slice);
	}

	return (status.ok ());
}

int nano::store::rocksdb::get (store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val & value_a)
{
	::rocksdb::ReadOptions options;
	::rocksdb::PinnableSlice slice;
	::rocksdb::Status status;
	if (is_read (transaction_a))
	{
		status = db (transaction_a)->Get (snapshot_options (transaction_a), table_a, key_a, &slice);
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

auto nano::store::rocksdb::is_read (nano::store::transaction const & transaction_a) -> bool
{
	return (dynamic_cast<nano::store::read_transaction const *> (&transaction_a) != nullptr);
}

auto nano::store::rocksdb::iter (nano::store::transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a) -> ::rocksdb::Iterator *
{
	// Don't fill the block cache for any blocks read as a result of an iterator
	if (is_read (transaction_a))
	{
		auto read_options = snapshot_options (transaction_a);
		read_options.fill_cache = false;
		return db (transaction_a)->NewIterator (read_options, table_a);
	}
	else
	{
		::rocksdb::ReadOptions ropts;
		ropts.fill_cache = false;
		return tx (transaction_a)->GetIterator (ropts, table_a);
	}
}

int nano::store::rocksdb::put (store::write_transaction const & transaction_a, ::rocksdb::ColumnFamilyHandle * table_a, nano::store::rocksdb::db_val const & key_a, nano::store::rocksdb::db_val const & value_a)
{
	auto txn = tx (transaction_a);
	return txn->Put (table_a, key_a, value_a).code ();
}

auto nano::store::rocksdb::snapshot_options (nano::store::transaction const & transaction_a) -> ::rocksdb::ReadOptions &
{
	debug_assert (nano::store::rocksdb::is_read (transaction_a));
	auto internals = reinterpret_cast<std::pair<::rocksdb::DB *, ::rocksdb::ReadOptions> *> (transaction_a.get_handle ());
	return internals->second;
}

auto nano::store::rocksdb::tx (store::transaction const & transaction_a) -> ::rocksdb::Transaction *
{
	debug_assert (!is_read (transaction_a));
	return static_cast<std::pair<::rocksdb::TransactionDB *, ::rocksdb::Transaction *> *> (transaction_a.get_handle ())->second;
}
