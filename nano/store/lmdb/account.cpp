#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/account.hpp>
#include <nano/store/lmdb/db_val.hpp>
#include <nano/store/lmdb/lmdb.hpp>

nano::store::lmdb::account::account (nano::store::lmdb::component & store_a) :
	store (store_a){};

void nano::store::lmdb::account::put (store::write_transaction const & transaction, nano::account const & account, nano::account_info const & info)
{
	auto status = store.put (transaction, tables::accounts, account, info);
	store.release_assert_success (status);
}

bool nano::store::lmdb::account::get (store::transaction const & transaction, nano::account const & account, nano::account_info & info)
{
	nano::store::lmdb::db_val value;
	auto status1 (store.get (transaction, tables::accounts, account, value));
	release_assert (store.success (status1) || store.not_found (status1));
	bool result (true);
	if (store.success (status1))
	{
		nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = info.deserialize (stream);
	}
	return result;
}

void nano::store::lmdb::account::del (store::write_transaction const & transaction_a, nano::account const & account_a)
{
	auto status = store.del (transaction_a, tables::accounts, account_a);
	store.release_assert_success (status);
}

bool nano::store::lmdb::account::exists (store::transaction const & transaction_a, nano::account const & account_a)
{
	auto iterator (begin (transaction_a, account_a));
	return iterator != end () && nano::account (iterator->first) == account_a;
}

size_t nano::store::lmdb::account::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::accounts);
}

auto nano::store::lmdb::account::begin (store::transaction const & transaction, nano::account const & account) const -> iterator
{
	return store.make_iterator<nano::account, nano::account_info> (transaction, tables::accounts, account);
}

auto nano::store::lmdb::account::begin (store::transaction const & transaction) const -> iterator
{
	return store.make_iterator<nano::account, nano::account_info> (transaction, tables::accounts);
}

auto nano::store::lmdb::account::rbegin (store::transaction const & transaction_a) const -> iterator
{
	return store.make_iterator<nano::account, nano::account_info> (transaction_a, tables::accounts, false);
}

auto nano::store::lmdb::account::end () const -> iterator
{
	return iterator{ nullptr };
}

void nano::store::lmdb::account::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
