#include <nano/store/rocksdb/db_val.hpp>
#include <nano/store/rocksdb/unconfirmed_rep_weight.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>
#include <nano/store/rocksdb/utility.hpp>

nano::store::rocksdb::unconfirmed_rep_weight::unconfirmed_rep_weight (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_rep_weight", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

auto nano::store::rocksdb::unconfirmed_rep_weight::count (store::unconfirmed_transaction const & tx) -> uint64_t
{
	return rocksdb::count (tx, handle.get ());
}

void nano::store::rocksdb::unconfirmed_rep_weight::del (store::unconfirmed_write_transaction const & tx, nano::account const & key)
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

auto nano::store::rocksdb::unconfirmed_rep_weight::exists (store::unconfirmed_transaction const & tx, nano::account const & key) const -> bool
{
	return rocksdb::exists (tx, handle.get (), key);
}

auto nano::store::rocksdb::unconfirmed_rep_weight::get (store::unconfirmed_transaction const & tx, nano::account const & key) const -> std::optional<nano::uint128_t>
{
	rocksdb::db_val value;
	if (!rocksdb::get (tx, handle.get (), key, value))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		nano::amount result;
		nano::read (stream, result);
		return result;
	}
	else
	{
		return std::nullopt;
	}
}

void nano::store::rocksdb::unconfirmed_rep_weight::put (store::unconfirmed_write_transaction const & tx, nano::account const & key, nano::amount const & value)
{
	auto status = rocksdb::put (tx, handle.get (), key, value);
	release_assert (status == 0);
}
