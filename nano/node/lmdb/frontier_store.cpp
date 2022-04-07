#include <nano/node/lmdb/frontier_store.hpp>

#include <nano/node/lmdb/lmdb.hpp>

nano::frontier_store_mdb::frontier_store_mdb (nano::mdb_store & store) :
store{ store }
{
}

void nano::frontier_store_mdb::put (nano::write_transaction const & transaction, nano::block_hash const & hash, nano::account const & account)
{
	auto status = store.put (transaction, tables::frontiers, hash, account);
	release_assert_success (store, status);
}

nano::account nano::frontier_store_mdb::get (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	nano::db_val<MDB_val> value;
	auto status = store.get (transaction, tables::frontiers, hash, value);
	release_assert (store.success (status) || store.not_found (status));
	nano::account result{};
	if (store.success (status))
	{
		result = static_cast<nano::account> (value);
	}
	return result;
}

void nano::frontier_store_mdb::del (nano::write_transaction const & transaction, nano::block_hash const & hash)
{
	auto status = store.del (transaction, tables::frontiers, hash);
	release_assert_success (store, status);
}

nano::store_iterator<nano::block_hash, nano::account> nano::frontier_store_mdb::begin (nano::transaction const & transaction) const
{
	return static_cast<nano::store_partial<MDB_val, mdb_store> &> (store).make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers);
}

nano::store_iterator<nano::block_hash, nano::account> nano::frontier_store_mdb::begin (nano::transaction const & transaction, nano::block_hash const & hash) const
{
	return static_cast<nano::store_partial<MDB_val, mdb_store> &> (store).template make_iterator<nano::block_hash, nano::account> (transaction, tables::frontiers, nano::db_val<MDB_val> (hash));
}

nano::store_iterator<nano::block_hash, nano::account> nano::frontier_store_mdb::end () const
{
	return nano::store_iterator<nano::block_hash, nano::account> (nullptr);
}

void nano::frontier_store_mdb::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const
{
	parallel_traversal<nano::uint256_t> (
	[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}