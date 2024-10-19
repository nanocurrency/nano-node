#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/rocksdb/final_vote.hpp>
#include <nano/store/rocksdb/rocksdb.hpp>

nano::store::rocksdb::final_vote::final_vote (nano::store::rocksdb::component & store) :
	store{ store } {};

bool nano::store::rocksdb::final_vote::put (store::write_transaction const & transaction, nano::qualified_root const & root, nano::block_hash const & hash)
{
	nano::store::rocksdb::db_val value;
	auto status = store.get (transaction, tables::final_votes, root, value);
	release_assert (store.success (status) || store.not_found (status));
	bool result (true);
	if (store.success (status))
	{
		result = static_cast<nano::block_hash> (value) == hash;
	}
	else
	{
		status = store.put (transaction, tables::final_votes, root, hash);
		store.release_assert_success (status);
	}
	return result;
}

std::vector<nano::block_hash> nano::store::rocksdb::final_vote::get (store::transaction const & transaction, nano::root const & root_a)
{
	std::vector<nano::block_hash> result;
	nano::qualified_root key_start{ root_a.raw, 0 };
	for (auto i = begin (transaction, key_start), n = end (transaction); i != n && nano::qualified_root{ i->first }.root () == root_a; ++i)
	{
		result.push_back (i->second);
	}
	return result;
}

void nano::store::rocksdb::final_vote::del (store::write_transaction const & transaction, nano::root const & root)
{
	std::vector<nano::qualified_root> final_vote_qualified_roots;
	for (auto i = begin (transaction, nano::qualified_root{ root.raw, 0 }), n = end (transaction); i != n && nano::qualified_root{ i->first }.root () == root; ++i)
	{
		final_vote_qualified_roots.push_back (i->first);
	}

	for (auto & final_vote_qualified_root : final_vote_qualified_roots)
	{
		auto status = store.del (transaction, tables::final_votes, final_vote_qualified_root);
		store.release_assert_success (status);
	}
}

size_t nano::store::rocksdb::final_vote::count (store::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::final_votes);
}

void nano::store::rocksdb::final_vote::clear (store::write_transaction const & transaction_a, nano::root const & root_a)
{
	del (transaction_a, root_a);
}

void nano::store::rocksdb::final_vote::clear (store::write_transaction const & transaction_a)
{
	store.drop (transaction_a, nano::tables::final_votes);
}

auto nano::store::rocksdb::final_vote::begin (store::transaction const & transaction, nano::qualified_root const & root) const -> iterator
{
	return store.make_iterator<nano::qualified_root, nano::block_hash> (transaction, tables::final_votes, root);
}

auto nano::store::rocksdb::final_vote::begin (store::transaction const & transaction) const -> iterator
{
	return store.make_iterator<nano::qualified_root, nano::block_hash> (transaction, tables::final_votes);
}

auto nano::store::rocksdb::final_vote::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ nullptr };
}

void nano::store::rocksdb::final_vote::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
