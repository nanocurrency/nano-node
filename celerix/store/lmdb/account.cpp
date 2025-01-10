#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/lmdb/account.hpp>
#include <celerix/store/lmdb/db_val.hpp>
#include <celerix/store/lmdb/lmdb.hpp>

celerix::store::lmdb::account::account (celerix::store::lmdb::component & store_a) :
	store (store_a){};

void celerix::store::lmdb::account::put (store::write_transaction const & transaction, celerix::account const & account, celerix::account_info const & info)
{
	auto status = store.put (transaction, tables::accounts, account, info);
	store.release_assert_success (status);
}

bool celerix::store::lmdb::account::get (store::transaction const & transaction, celerix::account const & account, celerix::account_info & info)
{
	celerix::store::lmdb::db_val value;
	auto status1 (store.get (transaction, tables::accounts, account, value));
	release_assert (store.success (status1) || store.not_found (status1));
	bool result (true);
	if (store.success (status1))
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = info.deserialize (stream);
	}
	return result;
}

void celerix::store::lmdb::account::del (store::write_transaction const & transaction_a, celerix::account const & account_a)
{
	auto status = store.del (transaction_a, tables::accounts, account_a);
	store.release_assert_success (status);
}

bool celerix::store::lmdb::account::exists (store::transaction const & transaction_a, celerix::account const & account_a)
{
	auto iterator (begin (transaction_a, account_a));
	return iterator != end (transaction_a) && celerix::account (iterator->first) == account_a;
}

size_t celerix::store::lmdb::account::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::accounts);
}

auto celerix::store::lmdb::account::begin (store::transaction const & transaction, celerix::account const & account) const -> iterator
{
	lmdb::db_val val{ account };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction), accounts_handle, val) } };
}

auto celerix::store::lmdb::account::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), accounts_handle) } };
}

auto celerix::store::lmdb::account::end (store::transaction const & tx) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (tx), accounts_handle) } };
}

void celerix::store::lmdb::account::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint256_t> (
	[&action_a, this] (celerix::uint256_t const & start, celerix::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
