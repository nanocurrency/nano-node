#include <celerix/store/rocksdb/online_weight.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/store/rocksdb/utility.hpp>

celerix::store::rocksdb::online_weight::online_weight (celerix::store::rocksdb::component & store_a) :
	store{ store_a }
{
}

void celerix::store::rocksdb::online_weight::put (store::write_transaction const & transaction, uint64_t time, celerix::amount const & amount)
{
	auto status = store.put (transaction, tables::online_weight, time, amount);
	store.release_assert_success (status);
}

void celerix::store::rocksdb::online_weight::del (store::write_transaction const & transaction, uint64_t time)
{
	auto status = store.del (transaction, tables::online_weight, time);
	store.release_assert_success (status);
}

auto celerix::store::rocksdb::online_weight::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::begin (store.db.get (), rocksdb::tx (transaction), store.table_to_column_family (tables::online_weight)) } };
}

auto celerix::store::rocksdb::online_weight::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::end (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::online_weight)) } };
}

size_t celerix::store::rocksdb::online_weight::count (store::transaction const & transaction) const
{
	return store.count (transaction, tables::online_weight);
}

void celerix::store::rocksdb::online_weight::clear (store::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::online_weight);
	store.release_assert_success (status);
}
