#include <nano/secure/account_info.hpp>
#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/db_val.hpp>
#include <nano/store/ledger/account.hpp>

namespace nano::store::ledger
{
account_view::account_view (nano::store::backend & backend_a) :
	backend{ backend_a }
{
}

void account_view::put (nano::store::write_transaction const & txn, nano::account const & account, nano::account_info const & info)
{
	auto status = backend.put (txn, nano::store::table::accounts, account, info);
	backend.release_assert_success (status);
}

bool account_view::get (nano::store::transaction const & txn, nano::account const & account, nano::account_info & info) const
{
	nano::store::db_val value;
	auto status = backend.get (txn, nano::store::table::accounts, account, value);
	release_assert (backend.success (status) || backend.not_found (status), backend.error_string (status));
	bool result = true;
	if (backend.success (status))
	{
		// TODO: Use db_val serialization operators
		nano::bufferstream stream{ reinterpret_cast<uint8_t const *> (value.data ()), value.size () };
		result = info.deserialize (stream);
	}
	return result;
}

std::optional<nano::account_info> account_view::get (nano::store::transaction const & txn, nano::account const & account) const
{
	nano::account_info info;
	bool error = get (txn, account, info);
	if (error)
	{
		return std::nullopt;
	}
	return info;
}

void account_view::del (nano::store::write_transaction const & txn, nano::account const & account)
{
	auto status = backend.del (txn, nano::store::table::accounts, account);
	backend.release_assert_success (status);
}

bool account_view::exists (nano::store::transaction const & txn, nano::account const & account) const
{
	return backend.exists (txn, nano::store::table::accounts, account);
}

size_t account_view::count (nano::store::transaction const & txn) const
{
	return backend.count (txn, nano::store::table::accounts);
}

auto account_view::begin (nano::store::transaction const & txn, nano::account const & account) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::accounts, account) };
}

auto account_view::begin (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.begin (txn, nano::store::table::accounts) };
}

auto account_view::rbegin (nano::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ std::prev (end (txn)) };
}

auto account_view::rend (nano::store::transaction const & txn) const -> reverse_iterator
{
	return reverse_iterator{ end (txn) };
}

auto account_view::end (nano::store::transaction const & txn) const -> iterator
{
	return iterator{ backend.end (txn, nano::store::table::accounts) };
}

auto account_view::crawl (nano::store::transaction const & txn, nano::account const & start) const -> crawler
{
	return crawler{ *this, txn, start };
}

void account_view::for_each_par (std::function<void (nano::store::read_transaction const &, iterator, iterator)> const & action) const
{
	parallel_traversal<nano::uint256_t> (
	[&action, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction = this->backend.tx_begin_read ();
		action (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
}