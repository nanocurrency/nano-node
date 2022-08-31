#include <nano/node/rocksdb/frontier_store.hpp>
#include <nano/node/rocksdb/rocksdb.hpp>
#include <nano/secure/parallel_traversal.hpp>

nano::rocksdb::frontier_store::frontier_store (nano::rocksdb::store & store) :
	store{ store }
{
}

void nano::rocksdb::frontier_store::put (nano::write_transaction const & transaction, nano::block_hash const & block, nano::account const & account)
{
	auto status = store.put (transaction, tables::frontiers, block, account);
	store.release_assert_success (status);
}

nano::account nano::rocksdb::frontier_store::get (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::db_val<::rocksdb::Slice> value;
	auto status = store.get (transaction, tables::frontiers, hash, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::account result{};
	if (store.success (status))
	{
		result = static_cast<nano::account> (value);
	}
	return result;
}

void nano::rocksdb::frontier_store::del (nano::write_transaction const & transaction, nano::block_hash const & hash)
{
	auto status = store.del (transaction, tables::frontiers, hash);
	store.release_assert_success (status);
}

nano::store_iterator<nano::block_hash, nano::account> nano::rocksdb::frontier_store::begin (nano::transaction const & transaction) const
{
	return store.make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers);
}

nano::store_iterator<nano::block_hash, nano::account> nano::rocksdb::frontier_store::begin (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers, hash);
}

nano::store_iterator<nano::block_hash, nano::account> nano::rocksdb::frontier_store::end () const
{
	return nano::store_iterator<nano::block_hash, nano::account> (nullptr);
}

void nano::rocksdb::frontier_store::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
