#include <celerix/secure/parallel_traversal.hpp>
#include <celerix/store/lmdb/lmdb.hpp>
#include <celerix/store/lmdb/pruned.hpp>

celerix::store::lmdb::pruned::pruned (celerix::store::lmdb::component & store_a) :
	store{ store_a } {};

void celerix::store::lmdb::pruned::put (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a)
{
	auto status = store.put (transaction_a, tables::pruned, hash_a, nullptr);
	store.release_assert_success (status);
}

void celerix::store::lmdb::pruned::del (store::write_transaction const & transaction_a, celerix::block_hash const & hash_a)
{
	auto status = store.del (transaction_a, tables::pruned, hash_a);
	store.release_assert_success (status);
}

bool celerix::store::lmdb::pruned::exists (store::transaction const & transaction_a, celerix::block_hash const & hash_a) const
{
	return store.exists (transaction_a, tables::pruned, hash_a);
}

celerix::block_hash celerix::store::lmdb::pruned::random (store::transaction const & transaction)
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

size_t celerix::store::lmdb::pruned::count (store::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::pruned);
}

void celerix::store::lmdb::pruned::clear (store::write_transaction const & transaction_a)
{
	auto status = store.drop (transaction_a, tables::pruned);
	store.release_assert_success (status);
}

auto celerix::store::lmdb::pruned::begin (store::transaction const & transaction, celerix::block_hash const & hash) const -> iterator
{
	lmdb::db_val val{ hash };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction), pruned_handle, val) } };
}

auto celerix::store::lmdb::pruned::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), pruned_handle) } };
}

auto celerix::store::lmdb::pruned::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), pruned_handle) } };
}

void celerix::store::lmdb::pruned::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<celerix::uint256_t> (
	[&action_a, this] (celerix::uint256_t const & start, celerix::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
