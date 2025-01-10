#include <celerix/lib/numbers.hpp>
#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/rep_weight.hpp>

#include <iostream>
#include <stdexcept>

celerix::store::lmdb::rep_weight::rep_weight (celerix::store::lmdb::component & store_a) :
	store{ store_a }
{
}

uint64_t celerix::store::lmdb::rep_weight::count (store::transaction const & txn_a)
{
	return store.count (txn_a, tables::rep_weights);
}

celerix::uint128_t celerix::store::lmdb::rep_weight::get (store::transaction const & txn_a, celerix::account const & representative_a)
{
	celerix::store::lmdb::db_val value;
	auto status = store.get (txn_a, tables::rep_weights, representative_a, value);
	release_assert (store.success (status) || store.not_found (status));
	celerix::uint128_t weight{ 0 };
	if (store.success (status))
	{
		celerix::uint128_union weight_union{ value };
		weight = weight_union.number ();
	}
	return weight;
}

void celerix::store::lmdb::rep_weight::put (store::write_transaction const & txn_a, celerix::account const & representative_a, celerix::uint128_t const & weight_a)
{
	celerix::uint128_union weight{ weight_a };
	auto status = store.put (txn_a, tables::rep_weights, representative_a, weight);
	store.release_assert_success (status);
}

void celerix::store::lmdb::rep_weight::del (store::write_transaction const & txn_a, celerix::account const & representative_a)
{
	auto status = store.del (txn_a, tables::rep_weights, representative_a);
	store.release_assert_success (status);
}

auto celerix::store::lmdb::rep_weight::begin (store::transaction const & transaction_a, celerix::account const & representative_a) const -> iterator
{
	lmdb::db_val val{ representative_a };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction_a), rep_weights_handle, val) } };
}

auto celerix::store::lmdb::rep_weight::begin (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction_a), rep_weights_handle) } };
}

auto celerix::store::lmdb::rep_weight::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), rep_weights_handle) } };
}

void celerix::store::lmdb::rep_weight::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint256_t> (
	[&action_a, this] (celerix::uint256_t const & start, celerix::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
