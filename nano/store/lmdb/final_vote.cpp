#include <nano/secure/parallel_traversal.hpp>
#include <nano/store/lmdb/final_vote.hpp>
#include <nano/store/lmdb/lmdb.hpp>

nano::store::lmdb::final_vote::final_vote (nano::store::lmdb::component & store) :
	store{ store } {};

bool nano::store::lmdb::final_vote::put (store::write_transaction const & transaction, nano::qualified_root const & root, nano::block_hash const & hash)
{
	nano::store::lmdb::db_val value;
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

std::optional<nano::block_hash> nano::store::lmdb::final_vote::get (store::transaction const & transaction, nano::qualified_root const & qualified_root_a)
{
	nano::store::lmdb::db_val result;
	auto status = store.get (transaction, tables::final_votes, qualified_root_a, result);
	std::optional<nano::block_hash> final_vote_hash;
	if (store.success (status))
	{
		final_vote_hash = static_cast<nano::block_hash> (result);
	}
	return final_vote_hash;
}

void nano::store::lmdb::final_vote::del (store::write_transaction const & transaction, nano::qualified_root const & root)
{
	auto status = store.del (transaction, tables::final_votes, root);
	store.release_assert_success (status);
}

size_t nano::store::lmdb::final_vote::count (store::transaction const & transaction_a) const
{
	return store.count (transaction_a, tables::final_votes);
}

void nano::store::lmdb::final_vote::clear (store::write_transaction const & transaction_a)
{
	store.drop (transaction_a, nano::tables::final_votes);
}

auto nano::store::lmdb::final_vote::begin (store::transaction const & transaction, nano::qualified_root const & root) const -> iterator
{
	lmdb::db_val val{ root };
	return iterator{ store::iterator{ lmdb::iterator::lower_bound (store.env.tx (transaction), final_votes_handle, val) } };
}

auto nano::store::lmdb::final_vote::begin (store::transaction const & transaction) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::begin (store.env.tx (transaction), final_votes_handle) } };
}

auto nano::store::lmdb::final_vote::end (store::transaction const & transaction_a) const -> iterator
{
	return iterator{ store::iterator{ lmdb::iterator::end (store.env.tx (transaction_a), final_votes_handle) } };
}

void nano::store::lmdb::final_vote::for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const
{
	parallel_traversal<nano::uint512_t> (
	[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
		auto transaction (this->store.tx_begin_read ());
		action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end (transaction));
	});
}
