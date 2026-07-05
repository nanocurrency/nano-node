#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/extended/account_receivable_by_amount.hpp>

#include <limits>

namespace nano::store::ledger
{
account_receivable_by_amount_view::account_receivable_by_amount_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void account_receivable_by_amount_view::put (nano::store::write_transaction const & txn, nano::account_receivable_by_amount_key const & key, nano::account_receivable_by_amount_info const & info)
{
	auto status = backend.put (txn, nano::store::table::account_receivable_by_amount, key, info);
	backend.release_assert_success (status);
}

void account_receivable_by_amount_view::del (nano::store::write_transaction const & txn, nano::account_receivable_by_amount_key const & key)
{
	auto status = backend.del (txn, nano::store::table::account_receivable_by_amount, key);
	backend.release_assert_success (status);
}

bool account_receivable_by_amount_view::empty (nano::store::transaction const & txn) const
{
	return backend.empty (txn, nano::store::table::account_receivable_by_amount);
}

uint64_t account_receivable_by_amount_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::account_receivable_by_amount);
}

void account_receivable_by_amount_view::clear ()
{
	auto status = backend.clear (nano::store::table::account_receivable_by_amount);
	backend.release_assert_success (status);
}

auto account_receivable_by_amount_view::begin (nano::store::transaction const & txn, nano::account_receivable_by_amount_key const & key) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::account_receivable_by_amount, key) };
}

auto account_receivable_by_amount_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::account_receivable_by_amount) };
}

auto account_receivable_by_amount_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::account_receivable_by_amount) };
}

auto account_receivable_by_amount_view::upper_bound (nano::store::transaction const & txn, nano::account const & account) const -> iterator
{
	auto it = account.number () == std::numeric_limits<nano::account::underlying_type>::max () ? end (txn) : begin (txn, nano::account_receivable_by_amount_key{ nano::account{ account.number () + 1 }, 0, 0 });
	if (it == begin (txn))
	{
		return end (txn);
	}
	--it;
	return it->first.account == account ? iterator{ std::move (it) } : end (txn);
}

auto account_receivable_by_amount_view::rupper_bound (nano::store::transaction const & txn, nano::account const & account) const -> reverse_iterator
{
	return reverse_iterator{ upper_bound (txn, account) };
}

auto account_receivable_by_amount_view::rend (nano::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ end (txn) };
}
}
