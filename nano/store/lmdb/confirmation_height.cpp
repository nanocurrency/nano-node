#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/lmdb/confirmation_height.hpp>
#include <celerix/store/lmdb/lmdb.hpp>

celerix::store::lmdb::confirmation_height::confirmation_height (celerix::store::lmdb::component & store) :
	store{ store }
{
}

void celerix::store::lmdb::confirmation_height::put (store::write_transaction const & transaction, celerix::account const & account, celerix::confirmation_height_info const & confirmation_height_info)
{
	auto status = store.put (transaction, tables::confirmation_height, account, confirmation_height_info);
	store.release_assert_success (status);
}

bool celerix::store::lmdb::confirmation_height::get (store::transaction const & transaction, celerix::account const & account, celerix::confirmation_height_info & confirmation_height_info)
{
	celerix::store::lmdb::db_val value;
	auto status = store.get (transaction, tables::confirmation_height, account, value);
	release_assert (store.success (status) || store.not_found (status));
	bool result (true);
	if (store.success (status))
	{
		celerix::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		result = confirmation_height_info.deserialize (stream);
	}
	if (result)
	{
		confirmation_height_info.height = 0;
		confirmation_height_info.frontier = 0;
	}

	return result;
}

bool celerix::store::lmdb::confirmation_height::exists (store::transaction const & transaction, celerix::account const & account) const
{
	return store.exists (transaction, tables::confirmation_height, account);
}

void celerix::store::lmdb::confirmation_height::del (store::write_transaction const & transaction, celerix::account const & account)
{
	auto status = store.del (transaction, tables::confirmation_height, account);
	store.release_assert_success (status);
}

uint64_t celerix::store::lmdb::confirmation_height::count (store::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::confirmation_height);
}

void celerix::store::lmdb::confirmation_height::clear (store::write_transaction const & transaction_a, celerix::account const & account_a)
{
	del (transaction_a, account_a);
}

void celerix::store::lmdb::confirmation_height::clear (store::write_transaction const & transaction_a)
{
	store.drop (transaction_a, celerix::tables::confirmation_height);
}

auto celerix::store::lmdb::confirmation_height::begin (store::transaction const & transaction, celerix::account const & account) const -> iterator
{
	lmdb::db_val val{ account };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction), confirmation_height_handle, val) } };
}

auto celerix::store::lmdb::confirmation_height::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), confirmation_height_handle) } };
}

auto celerix::store::lmdb::confirmation_height::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), confirmation_height_handle) } };
}

void celerix::store::lmdb::confirmation_height::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint256_t> (
	[&action_a, this] (celerix::uint256_t const & start, celerix::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
