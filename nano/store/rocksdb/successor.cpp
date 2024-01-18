#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/successor.hpp>

nano::store::rocksdb::successor::successor (nano::store::rocksdb::component & store) :
	store{ store }
{
}

void nano::store::rocksdb::successor::put (store::write_transaction const & transaction, nano::block_hash const & hash, nano::block_hash const & successor)
{
	auto status = store.put (transaction, tables::successor, hash, successor);
	store.release_assert_success (status);
}

nano::block_hash nano::store::rocksdb::successor::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	db_val value;
	auto status = store.get (transaction, tables::successor, hash, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::block_hash result{ 0 };
	if (store.success (status))
	{
		result = static_cast<nano::block_hash> (value);
	}
	return result;
}

void nano::store::rocksdb::successor::del (store::write_transaction const & transaction, nano::block_hash const & hash)
{
	auto status = store.del (transaction, tables::successor, hash);
	store.release_assert_success (status);
}
