#include <nano/store/rocksdb/peer.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

nano::store::rocksdb::peer::peer (nano::store::rocksdb::component & store) :
	store{ store } {};

void nano::store::rocksdb::peer::put (store::write_transaction const & transaction, nano::endpoint_key const & endpoint, nano::millis_t timestamp)
{
	auto status = store.put (transaction, tables::peers, endpoint, timestamp);
	store.release_assert_success (status);
}

nano::millis_t nano::store::rocksdb::peer::get (store::transaction const & transaction, nano::endpoint_key const & endpoint) const
{
	nano::millis_t result{ 0 };
	db_val value;
	auto status = store.get (transaction, tables::peers, endpoint, value);
	release_assert (store.success (status) || store.not_found (status));
	if (store.success (status) && value.size () > 0)
	{
		result = static_cast<nano::millis_t> (value);
	}
	return result;
}

void nano::store::rocksdb::peer::del (store::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
}

bool nano::store::rocksdb::peer::exists (store::transaction const & transaction, nano::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t nano::store::rocksdb::peer::count (store::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void nano::store::rocksdb::peer::clear (store::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	store.release_assert_success (status);
}

auto nano::store::rocksdb::peer::begin (store::transaction const & transaction) const -> iterator
{
	return store.make_iterator<nano::endpoint_key, nano::millis_t> (transaction, tables::peers);
}

auto nano::store::rocksdb::peer::end () const -> iterator
{
	return iterator{ nullptr };
}
