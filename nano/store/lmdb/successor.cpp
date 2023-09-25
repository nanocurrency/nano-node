#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/successor.hpp>

nano::store::lmdb::successor::successor (nano::store::lmdb::component & store) :
	store{ store }
{
}

void nano::store::lmdb::successor::put (store::write_transaction const & transaction, nano::block_hash const & hash, nano::block_hash const & successor)
{
	debug_assert (!hash.is_zero ());
	debug_assert (!successor.is_zero ());
	auto status = store.put (transaction, tables::successor, hash, successor);
	store.release_assert_success (status);
}

nano::block_hash nano::store::lmdb::successor::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	store::db_val<MDB_val> value;
	auto status = store.get (transaction, tables::successor, hash, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::block_hash result{ 0 };
	if (store.success (status))
	{
		result = static_cast<nano::block_hash> (value);
	}
	return result;
}

void nano::store::lmdb::successor::del (store::write_transaction const & transaction, nano::block_hash const & hash)
{
	debug_assert (!hash.is_zero ());
	auto status = store.del (transaction, tables::successor, hash);
	store.release_assert_success (status);
}
