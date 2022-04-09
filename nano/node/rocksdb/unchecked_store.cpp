#include <nano/node/lmdb/unchecked_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>

nano::unchecked_store_rocksdb::unchecked_store_rocksdb (nano::rocksdb_store & store_a) :
	store (store_a){};

void nano::unchecked_store_rocksdb::clear (nano::write_transaction const & transaction_a)
{
	auto status = store.drop (transaction_a, tables::unchecked);
	release_assert_success (store, status);
}

void nano::unchecked_store_rocksdb::put (nano::write_transaction const & transaction_a, nano::hash_or_account const & dependency, nano::unchecked_info const & info)
{
	auto status = store.put (transaction_a, tables::unchecked, nano::unchecked_key{ dependency, info.block->hash () }, info);
	release_assert_success (store, status);
}

bool nano::unchecked_store_rocksdb::exists (nano::transaction const & transaction_a, nano::unchecked_key const & key)
{
	nano::rocksdb_val value;
	auto status = store.get (transaction_a, tables::unchecked, key, value);
	release_assert (store.success (status) || store.not_found (status));
	return store.success (status);
}

void nano::unchecked_store_rocksdb::del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a)
{
	auto status (store.del (transaction_a, tables::unchecked, key_a));
	release_assert_success (store, status);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::unchecked_store_rocksdb::end () const
{
	return nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::unchecked_store_rocksdb::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction, tables::unchecked);
}

nano::store_iterator<nano::unchecked_key, nano::unchecked_info> nano::unchecked_store_rocksdb::lower_bound (nano::transaction const & transaction, nano::unchecked_key const & key) const
{
	return store.make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction, tables::unchecked, key);
}

size_t nano::unchecked_store_rocksdb::count (nano::transaction const & transaction_a)
{
	return store.count (transaction_a, tables::unchecked);
}

void nano::unchecked_store_rocksdb::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		nano::unchecked_key key_start (start);
		nano::unchecked_key key_end (end);
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->lower_bound (transaction, key_start), !is_last ? this->lower_bound (transaction, key_end) : this->end ());
	});
}
