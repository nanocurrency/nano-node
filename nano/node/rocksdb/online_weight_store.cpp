#include <nano/node/rocksdb/online_weight_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>

nano::rocksdb::online_weight_store::online_weight_store (nano::rocksdb_store & store_a) :
	store{ store_a }
{
}

void nano::rocksdb::online_weight_store::put (nano::write_transaction const & transaction, uint64_t time, nano::amount const & amount)
{
	auto status = store.put (transaction, tables::online_weight, time, amount);
	store.release_assert_success (status);
}

void nano::rocksdb::online_weight_store::del (nano::write_transaction const & transaction, uint64_t time)
{
	auto status = store.del (transaction, tables::online_weight, time);
	store.release_assert_success (status);
}

nano::store_iterator<uint64_t, nano::amount> nano::rocksdb::online_weight_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<uint64_t, nano::amount> (transaction, tables::online_weight);
}

nano::store_iterator<uint64_t, nano::amount> nano::rocksdb::online_weight_store::rbegin (nano::transaction const & transaction) const
{
	return store.make_iterator<uint64_t, nano::amount> (transaction, tables::online_weight, false);
}

nano::store_iterator<uint64_t, nano::amount> nano::rocksdb::online_weight_store::end () const
{
	return nano::store_iterator<uint64_t, nano::amount> (nullptr);
}

size_t nano::rocksdb::online_weight_store::count (nano::transaction const & transaction) const
{
	return store.count (transaction, tables::online_weight);
}

void nano::rocksdb::online_weight_store::clear (nano::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::online_weight);
	store.release_assert_success (status);
}
