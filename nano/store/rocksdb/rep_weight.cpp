#include <nano/lib/numbers.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/rocksdb/rep_weight.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>
#include <nano/store/rocksdb/utility.hpp>

#include <stdexcept>

nano::store::rocksdb::rep_weight::rep_weight (nano::store::rocksdb::component & store_a) :
	store{ store_a }
{
}

uint64_t nano::store::rocksdb::rep_weight::count (store::transaction const & txn_a)
{
	return store.count (txn_a, tables::rep_weights);
}

nano::uint128_t nano::store::rocksdb::rep_weight::get (store::transaction const & txn_a, nano::account const & representative_a)
{
	db_val value;
	auto status = store.get (txn_a, tables::rep_weights, representative_a, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::uint128_t weight{ 0 };
	if (store.success (status))
	{
		nano::uint128_union weight_union{ value };
		weight = weight_union.number ();
	}
	return weight;
}

void nano::store::rocksdb::rep_weight::put (store::write_transaction const & txn_a, nano::account const & representative_a, nano::uint128_t const & weight_a)
{
	nano::uint128_union weight{ weight_a };
	auto status = store.put (txn_a, tables::rep_weights, representative_a, weight);
	store.release_assert_success (status);
}

void nano::store::rocksdb::rep_weight::del (store::write_transaction const & txn_a, nano::account const & representative_a)
{
	auto status = store.del (txn_a, tables::rep_weights, representative_a);
	store.release_assert_success (status);
}

auto nano::store::rocksdb::rep_weight::begin (store::transaction const & txn_a, nano::account const & representative_a) const -> iterator
{
	rocksdb::db_val val{ representative_a };
	return iterator{ store::iterator{ rocksdb::iterator::lower_bound (store.db.get (), rocksdb::tx (txn_a), store.table_to_column_family (tables::rep_weights), val) } };
}

auto nano::store::rocksdb::rep_weight::begin (store::transaction const & txn_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::begin (store.db.get (), rocksdb::tx (txn_a), store.table_to_column_family (tables::rep_weights)) } };
}

auto nano::store::rocksdb::rep_weight::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::end (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::rep_weights)) } };
}

void nano::store::rocksdb::rep_weight::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
