#include <nano/lib/numbers.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/ledger/rep_weight.hpp>

#include <iostream>
#include <stdexcept>

namespace nano::store::ledger
{
rep_weight_view::rep_weight_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

uint64_t rep_weight_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::rep_weights);
}

nano::uint128_t rep_weight_view::get (nano::store::transaction const & txn, nano::account const & representative) const
{
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::rep_weights, representative, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	nano::uint128_t weight{ 0 };
	if (backend.success (status))
	{
		nano::uint128_union weight_union{ value };
		weight = weight_union.number ();
	}
	return weight;
}

void rep_weight_view::put (nano::store::write_transaction const & txn, nano::account const & representative, nano::uint128_t const & weight)
{
	nano::uint128_union weight_union{ weight };
	auto status = backend.put (txn, nano::store::table::rep_weights, representative, weight_union);
	backend.release_assert_success (status);
}

void rep_weight_view::del (nano::store::write_transaction const & txn, nano::account const & representative)
{
	auto status = backend.del (txn, nano::store::table::rep_weights, representative);
	backend.release_assert_success (status);
}

auto rep_weight_view::begin (nano::store::transaction const & txn, nano::account const & representative) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::rep_weights, representative) };
}

auto rep_weight_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::rep_weights) };
}

auto rep_weight_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::rep_weights) };
}

void rep_weight_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto txn = this->backend.tx_begin_read ();
		action (txn, this->begin (txn, start), !is_last ? this->begin (txn, end) : this->end (txn));
	});
}
}
