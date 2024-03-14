#include <nano/store/db_val_impl.hpp>
#include <nano/store/rocksdb/unconfirmed_received.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>
#include <nano/store/rocksdb/utility.hpp>

nano::store::rocksdb::unconfirmed_received::unconfirmed_received (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_received", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

void nano::store::rocksdb::unconfirmed_received::del (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key)
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

bool nano::store::rocksdb::unconfirmed_received::exists (store::unconfirmed_transaction const & tx, nano::pending_key const & key) const
{
	rocksdb::db_val junk;
	return !rocksdb::get (tx, handle.get (), key, junk);
}

void nano::store::rocksdb::unconfirmed_received::put (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key)
{
	auto status = rocksdb::put (tx, handle.get (), key, rocksdb::db_val{});
	release_assert (status == 0);
}
