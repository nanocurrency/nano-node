#include <nano/node/rocksdb/peer_store.hpp>

#include <nano/node/rocksdb/rocksdb.hpp>

nano::peer_store_rocksdb::peer_store_rocksdb (nano::rocksdb_store & store) :
	store{ store }
{
};

void nano::peer_store_rocksdb::put (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.put_key (transaction, tables::peers, endpoint);
	release_assert_success (store, status);
}

void nano::peer_store_rocksdb::del (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	release_assert_success (store, status);
}

bool nano::peer_store_rocksdb::exists (nano::transaction const & transaction, nano::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t nano::peer_store_rocksdb::count (nano::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void nano::peer_store_rocksdb::clear (nano::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	release_assert_success (store, status);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::peer_store_rocksdb::begin (nano::transaction const & transaction) const
{
	return static_cast<nano::store_partial<rocksdb::Slice, rocksdb_store> &> (store).template make_iterator<nano::endpoint_key, nano::no_value> (transaction, tables::peers);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::peer_store_rocksdb::end () const
{
	return nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr);
}
