#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/rocksdb/pending.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/unconfirmed_transaction.hpp>

nano::store::rocksdb::unconfirmed_pending::unconfirmed_pending (::rocksdb::DB & db)
{
	::rocksdb::ColumnFamilyOptions options;
	::rocksdb::ColumnFamilyHandle * handle;
	auto status = db.CreateColumnFamily (options, "unconfirmed_pending", &handle);
	release_assert (status.ok ());
	this->handle.reset (handle);
}

auto nano::store::rocksdb::unconfirmed_pending::del (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key) -> void
{
	auto status = rocksdb::del (tx, handle.get (), key);
	release_assert (status == 0);
}

auto nano::store::rocksdb::unconfirmed_pending::exists (store::unconfirmed_transaction const & tx, nano::pending_key const & key) -> bool
{
	rocksdb::db_val junk;
	return !rocksdb::get (tx, handle.get (), key, junk);
}

auto nano::store::rocksdb::unconfirmed_pending::get (store::unconfirmed_transaction const & tx, nano::pending_key const & key) -> std::optional<nano::pending_info>
{
	rocksdb::db_val value;
	if (!rocksdb::get (tx, handle.get (), key, value))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		nano::pending_info result;
		auto error = result.deserialize (stream);
		release_assert (!error);
		return result;
	}
	else
	{
		return std::nullopt;
	}
}

auto nano::store::rocksdb::unconfirmed_pending::lower_bound (store::unconfirmed_transaction const & tx, nano::account const & account, nano::block_hash const & hash) const -> std::optional<std::pair<nano::pending_key, nano::pending_info>>
{
	std::unique_ptr<::rocksdb::Iterator> iter{ rocksdb::iter (tx, handle.get ()) };
	nano::pending_key key{ account, hash };
	iter->Seek (rocksdb::db_val{ key });
	if (!iter->Valid ())
	{
		return std::nullopt;
	}
	rocksdb::db_val found_val = iter->key();
	nano::pending_key found = static_cast<nano::pending_key> (found_val);
	if (found.account != account)
	{
		return std::nullopt;
	}
	rocksdb::db_val info_val = iter->value ();
	nano::pending_info info = static_cast<nano::pending_info> (info_val);
	return std::make_pair (found, info);
}

void nano::store::rocksdb::unconfirmed_pending::put (store::unconfirmed_write_transaction const & tx, nano::pending_key const & key, nano::pending_info const & value)
{
	auto status = rocksdb::put (tx, handle.get (), key, value);
	release_assert (status == 0);
}
