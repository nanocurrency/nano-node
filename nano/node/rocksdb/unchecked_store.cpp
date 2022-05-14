#include <nano/node/lmdb/unchecked_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/secure/parallel_traversal.hpp>

nano::rocksdb::unchecked_store::unchecked_store (nano::rocksdb::store & store_a) :
	store (store_a){};

void nano::rocksdb::unchecked_store::clear (nano::write_transaction const & transaction_a)
{
	auto status = store.drop (transaction_a, tables::unchecked);
	store.release_assert_success (status);
}

void nano::rocksdb::unchecked_store::put (nano::write_transaction const & transaction_a, nano::hash_or_account const & dependency, nano::unchecked_info const & info)
{
	if (get (transaction_a, dependency.as_block_hash ()).size () > 1)
	{
		return;
	}
	auto status = store.put (transaction_a, tables::unchecked, nano::unchecked_key{ dependency, info.block->hash () }, info);
	store.release_assert_success (status);
}

bool nano::rocksdb::unchecked_store::exists (nano::transaction const & transaction_a, nano::unchecked_key const & key)
{
	nano::rocksdb_val value;
	auto status = store.get (transaction_a, tables::unchecked, key, value);
	release_assert (store.success (status) || store.not_found (status));
	return store.success (status);
}

void nano::rocksdb::unchecked_store::del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	auto status (store.del (transaction_a, tables::unchecked, key_a));
	store.release_assert_success (status);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::rocksdb::unchecked_store::end () const
{
	return nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::rocksdb::unchecked_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction, tables::unchecked);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::rocksdb::unchecked_store::lower_bound (nano::transaction const & transaction, nano::unchecked_key const & key) const
{
	return store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction, tables::unchecked, key);
}

size_t nano::rocksdb::unchecked_store::count (nano::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::unchecked);
}

void nano::rocksdb::unchecked_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		nano::unchecked_key key_start (start);
		nano::unchecked_key key_end (end);
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->lower_bound (transaction, key_start), !is_last ? this->lower_bound (transaction, key_end) : this->end ());
	});
}
