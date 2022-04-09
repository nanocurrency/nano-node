#include <nano/node/rocksdb/peer_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>

nano::rocksdb::peer_store::peer_store (nano::rocksdb_store & store) :
	store{ store } {};

void nano::rocksdb::peer_store::put (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.put_key (transaction, tables::peers, endpoint);
	release_assert_success (store, status);
}

void nano::rocksdb::peer_store::del (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	release_assert_success (store, status);
}

bool nano::rocksdb::peer_store::exists (nano::transaction const & transaction, nano::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t nano::rocksdb::peer_store::count (nano::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void nano::rocksdb::peer_store::clear (nano::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	release_assert_success (store, status);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::rocksdb::peer_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::endpoint_key, nano::no_value> (transaction, tables::peers);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::rocksdb::peer_store::end () const
{
	return nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr);
}
