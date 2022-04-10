#include <nano/node/lmdb/lmdb.hpp>
#include <nano/node/lmdb/peer_store.hpp>

nano::lmdb::peer_store::peer_store (nano::mdb_store & store) :
	store{ store } {};

void nano::lmdb::peer_store::put (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.put_key (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
}

void nano::lmdb::peer_store::del (nano::write_transaction const & transaction, nano::endpoint_key const & endpoint)
{
	auto status = store.del (transaction, tables::peers, endpoint);
	store.release_assert_success (status);
}

bool nano::lmdb::peer_store::exists (nano::transaction const & transaction, nano::endpoint_key const & endpoint) const
{
	return store.exists (transaction, tables::peers, endpoint);
}

size_t nano::lmdb::peer_store::count (nano::transaction const & transaction) const
{
	return store.count (transaction, tables::peers);
}

void nano::lmdb::peer_store::clear (nano::write_transaction const & transaction)
{
	auto status = store.drop (transaction, tables::peers);
	store.release_assert_success (status);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::lmdb::peer_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::endpoint_key, nano::no_value> (transaction, tables::peers);
}

nano::store_iterator<nano::endpoint_key, nano::no_value> nano::lmdb::peer_store::end () const
{
	return nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr);
}
