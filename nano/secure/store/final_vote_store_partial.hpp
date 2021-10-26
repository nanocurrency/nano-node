#pragma once

#include <nano/secure/store_partial.hpp>

namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action);
}

namespace nano
{
template <typename Val, typename Derived_Store>
class store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (store_partial<Val, Derived_Store> const & store, int const status);

template <typename Val, typename Derived_Store>
class final_vote_store_partial : public final_vote_store
{
private:
	nano::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit final_vote_store_partial (nano::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	bool put (nano::write_transaction const & transaction_a, nano::qualified_root const & root_a, nano::block_hash const & hash_a) override
	{
		nano::db_val<Val> value;
		auto status = store.get (transaction_a, tables::final_votes, nano::db_val<Val> (root_a), value);
		release_assert (store.success (status) || store.not_found (status));
		bool result (true);
		if (store.success (status))
		{
			result = static_cast<nano::block_hash> (value) == hash_a;
		}
		else
		{
			status = store.put (transaction_a, tables::final_votes, root_a, hash_a);
			release_assert_success (store, status);
		}
		return result;
	}

	std::vector<nano::block_hash> get (nano::transaction const & transaction_a, nano::root const & root_a) override
	{
		std::vector<nano::block_hash> result;
		nano::qualified_root key_start (root_a.raw, 0);
		for (auto i (begin (transaction_a, key_start)), n (end ()); i != n && nano::qualified_root (i->first).root () == root_a; ++i)
		{
			result.push_back (i->second);
		}
		return result;
	}

	void del (nano::write_transaction const & transaction_a, nano::root const & root_a) override
	{
		std::vector<nano::qualified_root> final_vote_qualified_roots;
		for (auto i (begin (transaction_a, nano::qualified_root (root_a.raw, 0))), n (end ()); i != n && nano::qualified_root (i->first).root () == root_a; ++i)
		{
			final_vote_qualified_roots.push_back (i->first);
		}

		for (auto & final_vote_qualified_root : final_vote_qualified_roots)
		{
			auto status (store.del (transaction_a, tables::final_votes, nano::db_val<Val> (final_vote_qualified_root)));
			release_assert_success (store, status);
		}
	}

	size_t count (nano::transaction const & transaction_a) const override
	{
		return store.count (transaction_a, tables::final_votes);
	}

	void clear (nano::write_transaction const & transaction_a, nano::root const & root_a) override
	{
		del (transaction_a, root_a);
	}

	void clear (nano::write_transaction const & transaction_a) override
	{
		store.drop (transaction_a, nano::tables::final_votes);
	}

	nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a, nano::qualified_root const & root_a) const override
	{
		return store.template make_iterator<nano::qualified_root, nano::block_hash> (transaction_a, tables::final_votes, nano::db_val<Val> (root_a));
	}

	nano::store_iterator<nano::qualified_root, nano::block_hash> begin (nano::transaction const & transaction_a) const override
	{
		return store.template make_iterator<nano::qualified_root, nano::block_hash> (transaction_a, tables::final_votes);
	}

	nano::store_iterator<nano::qualified_root, nano::block_hash> end () const override
	{
		return nano::store_iterator<nano::qualified_root, nano::block_hash> (nullptr);
	}

	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::qualified_root, nano::block_hash>, nano::store_iterator<nano::qualified_root, nano::block_hash>)> const & action_a) const override
	{
		parallel_traversal<nano::uint512_t> (
		[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
