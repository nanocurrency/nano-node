#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/peer.hpp>

celerix::store::lmdb::peer::peer (celerix::store::lmdb::component & store) :
	store{ store } {};

void celerix::store::lmdb::peer::put (store::write_transaction const & transaction, celerix::endpoint_key const & endpoint, celerix::millis_t timestamp)
{
	auto status = store.put (transaction, tables::peers, endpoint, timestamp);
	store.release_assert_success (status);
}

celerix::millis_t celerix::store::lmdb::peer::get (store::transaction const & transaction, celerix::endpoint_key const & endpoint) const
{
	celerix::millis_t result{ 0 };
	db_val value;
	auto status = store.get (transaction, tables::peers, endpoint, value);
	release_assert (store.success (status) || store.not_found (status));
	if (store.success (status) && value.size () > 0)
	{
		result = static_cast<celerix::millis_t> (value);
	}
	return result;
}

void celerix::store::lmdb::peer::del (store::write_transaction const & transaction, celerix::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
}

bool celerix::store::lmdb::peer::exists (store::transaction const & transaction, celerix::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t celerix::store::lmdb::peer::count (store::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void celerix::store::lmdb::peer::clear (store::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	store.release_assert_success (status);
}

auto celerix::store::lmdb::peer::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), peers_handle) } };
}

auto celerix::store::lmdb::peer::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), peers_handle) } };
}
