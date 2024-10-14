#include <nano/store/lmdb/lmdb.hpp>
#include <nano/store/lmdb/online_weight.hpp>

nano::store::lmdb::online_weight::online_weight (nano::store::lmdb::component & store_a) :
	store{ store_a }
{
}

void nano::store::lmdb::online_weight::put (store::write_transaction const & transaction, uint64_t time, nano::amount const & amount)
{
	auto status = store.put (transaction, tables::online_weight, time, amount);
	store.release_assert_success (status);
}

void nano::store::lmdb::online_weight::del (store::write_transaction const & transaction, uint64_t time)
{
	auto status = store.del (transaction, tables::online_weight, time);
	store.release_assert_success (status);
}

auto nano::store::lmdb::online_weight::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), online_weight_handle) } };
}

auto nano::store::lmdb::online_weight::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), online_weight_handle) } };
}

size_t nano::store::lmdb::online_weight::count (store::transaction const & transaction) const
{
	return store.count (transaction, tables::online_weight);
}

void nano::store::lmdb::online_weight::clear (store::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::online_weight);
	store.release_assert_success (status);
}
