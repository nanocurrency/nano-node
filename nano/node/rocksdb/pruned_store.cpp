#include <nano/node/rocksdb/pruned_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/secure/parallel_traversal.hpp>

nano::rocksdb::pruned_store::pruned_store (nano::rocksdb::store & store_a) :
	store{ store_a } {};

void nano::rocksdb::pruned_store::put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.put (transaction_a, tables::pruned, hash_a, nullptr);
	store.release_assert_success (status);
}

void nano::rocksdb::pruned_store::del (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::pruned, hash_a);
	store.release_assert_success (status);
}

bool nano::rocksdb::pruned_store::exists (nano::transaction const & transaction, nano::block_hash const & hash_a) const
{
	return store.exists (transaction, tables::pruned, hash_a);
}

nano::block_hash nano::rocksdb::pruned_store::random (nano::transaction const & transaction)
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	auto existing = begin (transaction, random_hash);
	if (existing == end ())
	{
		existing = begin (transaction);
	}
	return existing != end () ? existing->first : 0;
}

size_t nano::rocksdb::pruned_store::count (nano::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::pruned);
}

void nano::rocksdb::pruned_store::clear (nano::write_transaction const & transaction_a)
{
	auto status = store.drop (transaction_a, tables::pruned);
	store.release_assert_success (status);
}

nano::store_iterator<nano::block_hash, std::nullptr_t> nano::rocksdb::pruned_store::begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	return store.make_iterator<nano::block_hash, std::nullptr_t> (transaction_a, tables::pruned, hash_a);
}

nano::store_iterator<nano::block_hash, std::nullptr_t> nano::rocksdb::pruned_store::begin (nano::transaction const & transaction_a) const
{
	return store.make_iterator<nano::block_hash, std::nullptr_t> (transaction_a, tables::pruned);
}

nano::store_iterator<nano::block_hash, std::nullptr_t> nano::rocksdb::pruned_store::end () const
{
	return nano::store_iterator<nano::block_hash, std::nullptr_t> (nullptr);
}

void nano::rocksdb::pruned_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, std::nullptr_t>, nano::store_iterator<nano::block_hash, std::nullptr_t>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
