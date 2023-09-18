#include <nano/store/rocksdb/peer.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

nano::rocksdb::peer_store::peer_store (nano::rocksdb::store & store) :
	store{ store } {};

void nano::rocksdb::peer_store::put (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.put (transaction, tables::peers, endpoint, nullptr);
	store.release_assert_success (status);
}

void nano::rocksdb::peer_store::del (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
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
	store.release_assert_success (status);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::rocksdb::peer_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::endpoint_key, nano::no_value> (transaction, tables::peers);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::rocksdb::peer_store::end () const
{
	return nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr);
}
