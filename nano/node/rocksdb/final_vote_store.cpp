#include <nano/node/rocksdb/final_vote_store.hpp>

#include <nano/node/rocksdb/rocksdb.hpp>

nano::final_vote_store_rocksdb::final_vote_store_rocksdb (nano::rocksdb_store & store) :
	store{ store }
{
};

bool nano::final_vote_store_rocksdb::put (nano::write_transaction const & transaction, nano::qualified_root const & root, nano::block_hash const & hash)
{
	nano::rocksdb_val value;
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
		release_assert_success (store, status);
	}
	return result;
}

std::vector<nano::block_hash> nano::final_vote_store_rocksdb::get (nano::transaction const & transaction, nano::root const & root_a)
{
	std::vector<nano::block_hash> result;
	nano::qualified_root key_start{ root_a.raw, 0 };
	for (auto i = begin (transaction, key_start), n = end (); i != n && nano::qualified_root{ i->first }.root () == root_a; ++i)
	{
		result.push_back (i->second);
	}
	return result;
}

void nano::final_vote_store_rocksdb::del (nano::write_transaction const & transaction, nano::root const & root)
{
	std::vector<nano::qualified_root> final_vote_qualified_roots;
	for (auto i = begin (transaction, nano::qualified_root{ root.raw, 0 }), n = end (); i != n && nano::qualified_root{ i->first }.root () == root; ++i)
	{
		final_vote_qualified_roots.push_back (i->first);
	}

	for (auto & final_vote_qualified_root : final_vote_qualified_roots)
	{
		auto status = store.del (transaction, tables::final_votes, final_vote_qualified_root);
		release_assert_success (store, status);
	}
}

size_t nano::final_vote_store_rocksdb::count (nano::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::final_votes);
}

void nano::final_vote_store_rocksdb::clear (nano::write_transaction const & transaction_a, nano::root const & root_a)
{
	del (transaction_a, root_a);
}

void nano::final_vote_store_rocksdb::clear (nano::write_transaction const & transaction_a)
{
	store.drop (transaction_a, nano::tables::final_votes);
}

nano::store_iterator<nano::qualified_root, nano::block_hash> nano::final_vote_store_rocksdb::begin (nano::transaction const & transaction, nano::qualified_root const & root) const
{
	return static_cast<nano::store_partial<rocksdb::Slice, rocksdb_store> &> (store).template make_iterator<nano::qualified_root, nano::block_hash> (transaction, tables::final_votes, root);
}

nano::store_iterator<nano::qualified_root, nano::block_hash> nano::final_vote_store_rocksdb::begin (nano::transaction const & transaction) const
{
	return static_cast<nano::store_partial<rocksdb::Slice, rocksdb_store> &> (store).template make_iterator<nano::qualified_root, nano::block_hash> (transaction, tables::final_votes);
}

nano::store_iterator<nano::qualified_root, nano::block_hash> nano::final_vote_store_rocksdb::end () const
{
	return nano::store_iterator<nano::qualified_root, nano::block_hash> (nullptr);
}

void nano::final_vote_store_rocksdb::for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::qualified_root, nano::block_hash>, nano::store_iterator<nano::qualified_root, nano::block_hash>)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
	});
}
