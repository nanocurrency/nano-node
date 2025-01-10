#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/rocksdb/pruned.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/store/rocksdb/utility.hpp>

celerix::store::rocksdb::pruned::pruned (celerix::store::rocksdb::component & store_a) :
	store{ store_a } {};

void celerix::store::rocksdb::pruned::put (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a)
{
	auto status = store.put (transaction_a, tables::pruned, hash_a, nullptr);
	store.release_assert_success (status);
}

void celerix::store::rocksdb::pruned::del (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::pruned, hash_a);
	store.release_assert_success (status);
}

bool celerix::store::rocksdb::pruned::exists (store::transaction const & transaction, celerix::block_hash const & hash_a) const
{
	return store.exists (transaction, tables::pruned, hash_a);
}

celerix::block_hash celerix::store::rocksdb::pruned::random (store::transaction const & transaction)
{
	celerix::block_hash random_hash;
	celerix::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	auto existing = begin (transaction, random_hash);
	if (existing == end (transaction))
	{
		existing = begin (transaction);
	}
	return existing != end (transaction) ? existing->first : 0;
}

size_t celerix::store::rocksdb::pruned::count (store::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::pruned);
}

void celerix::store::rocksdb::pruned::clear (store::write_transaction const & transaction_a)
{
	auto status = store.drop (transaction_a, tables::pruned);
	store.release_assert_success (status);
}

auto celerix::store::rocksdb::pruned::begin (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const -> iterator
{
	rocksdb::db_val val{ hash_a };
	return iterator{ store::iterator{ rocksdb::iterator::lower_bound (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::pruned), val) } };
}

auto celerix::store::rocksdb::pruned::begin (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::begin (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::pruned)) } };
}

auto celerix::store::rocksdb::pruned::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ rocksdb::iterator::end (store.db.get (), rocksdb::tx (transaction_a), store.table_to_column_family (tables::pruned)) } };
}

void celerix::store::rocksdb::pruned::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint256_t> (
	[&action_a, this] (celerix::uint256_t const & start, celerix::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
