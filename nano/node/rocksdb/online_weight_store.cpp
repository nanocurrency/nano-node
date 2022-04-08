#include <nano/node/rocksdb/online_weight_store.hpp>

#include <nano/node/rocksdb/rocksdb.hpp>

nano::online_weight_store_rocksdb::online_weight_store_rocksdb (nano::rocksdb_store & store_a) :
	store{ store_a }
{
}

void nano::online_weight_store_rocksdb::put (nano::write_transaction const & transaction, uint64_t time, nano::amount const & amount)
{
	auto status = store.put (transaction, tables::online_weight, time, amount);
	release_assert_success (store, status);
}

void nano::online_weight_store_rocksdb::del (nano::write_transaction const & transaction, uint64_t time)
{
	auto status = store.del (transaction, tables::online_weight, time);
	release_assert_success (store, status);
}

nano::store_iterator<uint64_t, nano::amount> nano::online_weight_store_rocksdb::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<uint64_t, nano::amount> (transaction, tables::online_weight);
}

nano::store_iterator<uint64_t, nano::amount> nano::online_weight_store_rocksdb::rbegin (nano::transaction const & transaction) const
{
	return store.make_iterator<uint64_t, nano::amount> (transaction, tables::online_weight, false);
}

nano::store_iterator<uint64_t, nano::amount> nano::online_weight_store_rocksdb::end () const
{
	return nano::store_iterator<uint64_t, nano::amount> (nullptr);
}

size_t nano::online_weight_store_rocksdb::count (nano::transaction const & transaction) const
{
	return store.count (transaction, tables::online_weight);
}

void nano::online_weight_store_rocksdb::clear (nano::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::online_weight);
	release_assert_success (store, status);
}
