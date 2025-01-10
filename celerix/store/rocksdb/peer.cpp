#include <celerix/store/rocksdb/peer.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/store/rocksdb/utility.hpp>

celerix::store::rocksdb::peer::peer (celerix::store::rocksdb::component & store) :
	store{ store } {};

void celerix::store::rocksdb::peer::put (store::write_transaction const & transaction, celerix::endpoint_key const & endpoint, celerix::millis_t timestamp)
{
	auto status = store.put (transaction, tables::peers, endpoint, timestamp);
	store.release_assert_success (status);
}

celerix::millis_t celerix::store::rocksdb::peer::get (store::transaction const & transaction, celerix::endpoint_key const & endpoint) const
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

void celerix::store::rocksdb::peer::del (store::write_transaction const & transaction, celerix::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
}

bool celerix::store::rocksdb::peer::exists (store::transaction const & transaction, celerix::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t celerix::store::rocksdb::peer::count (store::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void celerix::store::rocksdb::peer::clear (store::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	store.release_assert_success (status);
}

auto celerix::store::rocksdb::peer::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::begin (store.db.get (), rocksdb::tx (transaction), store.table_to_column_family (tables::peers)) } };
}

auto celerix::store::rocksdb::peer::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::end (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::peers)) } };
}
