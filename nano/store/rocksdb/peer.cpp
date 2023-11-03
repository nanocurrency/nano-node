#include <nano/store/rocksdb/component.hpp>
#include <nano/store/rocksdb/peer.hpp>

nano::store::rocksdb::peer::peer (nano::store::rocksdb::component & store) :
	store{ store } {};

void nano::store::rocksdb::peer::put (store::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.put (transaction, tables::peers, endpoint, nullptr);
	store.release_assert_success (status);
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

nano::store::iterator<nano::endpoint_key, nano::no_value> nano::store::rocksdb::peer::begin (store::transaction const & transaction) const
{
	return store.make_iterator<nano::endpoint_key, nano::no_value> (transaction, tables::peers);
}

nano::store::iterator<nano::endpoint_key, nano::no_value> nano::store::rocksdb::peer::end () const
{
	return store::iterator<nano::endpoint_key, nano::no_value> (nullptr);
}
