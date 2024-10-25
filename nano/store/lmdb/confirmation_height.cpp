#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/confirmation_height.hpp>
#include <nano/store/lmdb/lmdb.hpp>

nano::store::lmdb::confirmation_height::confirmation_height (nano::store::lmdb::component & store) :
	store{ store }
{
}

void nano::store::lmdb::confirmation_height::put (store::write_transaction const & transaction, nano::account const & account, nano::confirmation_height_info const & confirmation_height_info)
{
	auto status = store.put (transaction, tables::confirmation_height, account, confirmation_height_info);
	store.release_assert_success (status);
}

bool nano::store::lmdb::confirmation_height::get (store::transaction const & transaction, nano::account const & account, nano::confirmation_height_info & confirmation_height_info)
{
	nano::store::lmdb::db_val value;
	auto status = store.get (transaction, tables::confirmation_height, account, value);
	release_assert (store.success (status) || store.not_found (status));
	bool result (true);
	if (store.success (status))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = confirmation_height_info.deserialize (stream);
	}
	if (result)
	{
		confirmation_height_info.height = 0;
		confirmation_height_info.frontier = 0;
	}

	return result;
}

bool nano::store::lmdb::confirmation_height::exists (store::transaction const & transaction, nano::account const & account) const
{
	return store.exists (transaction, tables::confirmation_height, account);
}

void nano::store::lmdb::confirmation_height::del (store::write_transaction const & transaction, nano::account const & account)
{
	auto status = store.del (transaction, tables::confirmation_height, account);
	store.release_assert_success (status);
}

uint64_t nano::store::lmdb::confirmation_height::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::confirmation_height);
}

void nano::store::lmdb::confirmation_height::clear (store::write_transaction const & transaction_a, nano::account const & account_a)
{
	del (transaction_a, account_a);
}

void nano::store::lmdb::confirmation_height::clear (store::write_transaction const & transaction_a)
{
	store.drop (transaction_a, nano::tables::confirmation_height);
}

auto nano::store::lmdb::confirmation_height::begin (store::transaction const & transaction, nano::account const & account) const -> iterator
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height, account);
}

auto nano::store::lmdb::confirmation_height::begin (store::transaction const & transaction) const -> iterator
{
	return store.make_iterator<nano::account, nano::confirmation_height_info> (transaction, tables::confirmation_height);
}

auto nano::store::lmdb::confirmation_height::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ nullptr };
}

void nano::store::lmdb::confirmation_height::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
