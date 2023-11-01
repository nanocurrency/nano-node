#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/rocksdb/component.hpp>
#include <nano/store/rocksdb/frontier.hpp>

nano::store::rocksdb::frontier::frontier (nano::store::rocksdb::component & store) :
	store{ store }
{
}

void nano::store::rocksdb::frontier::put (store::write_transaction const & transaction, nano::block_hash const & block, nano::account const & account)
{
	auto status = store.put (transaction, tables::frontiers, block, account);
	store.release_assert_success (status);
}

nano::account nano::store::rocksdb::frontier::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	db_val value;
	auto status = store.get (transaction, tables::frontiers, hash, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::account result{};
	if (store.success (status))
	{
		result = static_cast<nano::account> (value);
	}
	return result;
}

void nano::store::rocksdb::frontier::del (store::write_transaction const & transaction, nano::block_hash const & hash)
{
	auto status = store.del (transaction, tables::frontiers, hash);
	store.release_assert_success (status);
}

nano::store::iterator<nano::block_hash, nano::account> nano::store::rocksdb::frontier::begin (store::transaction const & transaction) const
{
	return store.make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers);
}

nano::store::iterator<nano::block_hash, nano::account> nano::store::rocksdb::frontier::begin (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers, hash);
}

nano::store::iterator<nano::block_hash, nano::account> nano::store::rocksdb::frontier::end () const
{
	return nano::store::iterator<nano::block_hash, nano::account> (nullptr);
}

void nano::store::rocksdb::frontier::for_each_par (std::function<void (store::read_transaction const &, nano::store::iterator<nano::block_hash, nano::account>, nano::store::iterator<nano::block_hash, nano::account>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
