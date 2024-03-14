#include <nano/store/rocksdb/db_val.hpp>
#include <nano/store/rocksdb/unconfirmed_successor.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>
#include <nano/store/rocksdb/utility.hpp>

nano::store::rocksdb::unconfirmed_successor::unconfirmed_successor (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_successor", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

void nano::store::rocksdb::unconfirmed_successor::del (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key)
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

bool nano::store::rocksdb::unconfirmed_successor::exists (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const
{
	rocksdb::db_val junk;
	return !rocksdb::get (tx, handle.get (), key, junk);
}

std::optional<nano::block_hash> nano::store::rocksdb::unconfirmed_successor::get (store::unconfirmed_transaction const & tx, nano::block_hash const & key) const
{
	rocksdb::db_val value;
	if (!rocksdb::get (tx, handle.get (), key, value))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		nano::block_hash result;
		nano::read (stream, result);
		return result;
	}
	else
	{
		return std::nullopt;
	}
}

void nano::store::rocksdb::unconfirmed_successor::put (store::unconfirmed_write_transaction const & tx, nano::block_hash const & key, nano::block_hash const & value)
{
	auto status = rocksdb::put (tx, handle.get (), key, value);
	release_assert (status == 0);
}
