#include <nano/secure/account_info.hpp>
#include <nano/store/db_val_impl.hpp>
#include <nano/store/rocksdb/unconfirmed_account.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>
#include <nano/store/rocksdb/utility.hpp>

nano::store::rocksdb::unconfirmed_account::unconfirmed_account (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_account", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

void nano::store::rocksdb::unconfirmed_account::del (store::unconfirmed_write_transaction const & tx, nano::account const & key)
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

bool nano::store::rocksdb::unconfirmed_account::exists (store::unconfirmed_transaction const & tx, nano::account const & key) const
{
	rocksdb::db_val junk;
	return !rocksdb::get (tx, handle.get (), key, junk);
}

auto nano::store::rocksdb::unconfirmed_account::get (store::unconfirmed_transaction const & tx, nano::account const & key) const -> std::optional<nano::account_info>
{
	rocksdb::db_val value;
	if (!rocksdb::get (tx, handle.get (), key, value))
	{
		nano::account_info result;
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error = result.deserialize (stream);
		release_assert (!error);
		return result;
	}
	else
	{
		return std::nullopt;
	}
}

void nano::store::rocksdb::unconfirmed_account::put (store::unconfirmed_write_transaction const & tx, nano::account const & key, nano::account_info const & value)
{
	auto status = rocksdb::put (tx, handle.get (), key, value);
	release_assert (status == 0);
}
