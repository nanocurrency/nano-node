#include <nano/node/rocksdb/reverse_link_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/secure/parallel_traversal.hpp>

nano::rocksdb::reverse_link_store::reverse_link_store (nano::rocksdb::store & store) :
	store{ store } {};

void nano::rocksdb::reverse_link_store::put (nano::write_transaction const & transaction_a, nano::block_hash const & send_block_hash_a, nano::block_hash const & receive_block_hash_a)
{
	auto status = store.put (transaction_a, tables::reverse_links, send_block_hash_a, receive_block_hash_a);
	store.release_assert_success (status);
}

nano::block_hash nano::rocksdb::reverse_link_store::get (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const
{
	nano::rocksdb_val value;
	auto status = store.get (transaction_a, tables::reverse_links, send_block_hash_a, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::block_hash result{ 0 };
	if (store.success (status))
	{
		result = static_cast<nano::block_hash> (value);
	}
	return result;
}

void nano::rocksdb::reverse_link_store::del (nano::write_transaction const & transaction_a, nano::block_hash const & send_block_hash_a)
{
	auto status = store.del (transaction_a, tables::reverse_links, send_block_hash_a);
	store.release_assert_success (status);
}

bool nano::rocksdb::reverse_link_store::exists (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const
{
	nano::rocksdb_val value;
	auto status = store.get (transaction_a, tables::reverse_links, send_block_hash_a, value);
	release_assert (store.success (status) || store.not_found (status));
	return store.success (status);
}

size_t nano::rocksdb::reverse_link_store::count (nano::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::reverse_links);
}

void nano::rocksdb::reverse_link_store::clear (nano::write_transaction const & transaction_a)
{
	store.drop (transaction_a, tables::reverse_links);
}

nano::store_iterator<nano::block_hash, nano::block_hash> nano::rocksdb::reverse_link_store::begin (nano::transaction const & transaction_a) const
{
	return store.make_iterator<nano::block_hash, nano::block_hash> (transaction_a, tables::reverse_links);
}

nano::store_iterator<nano::block_hash, nano::block_hash> nano::rocksdb::reverse_link_store::begin (nano::transaction const & transaction_a, nano::block_hash const & send_block_hash_a) const
{
	return store.make_iterator<nano::block_hash, nano::block_hash> (transaction_a, tables::reverse_links, send_block_hash_a);
}

nano::store_iterator<nano::block_hash, nano::block_hash> nano::rocksdb::reverse_link_store::end () const
{
	return nano::store_iterator<nano::block_hash, nano::block_hash> (nullptr);
}

void nano::rocksdb::reverse_link_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::block_hash>, nano::store_iterator<nano::block_hash, nano::block_hash>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction = this->store.tx_begin_read ();
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
