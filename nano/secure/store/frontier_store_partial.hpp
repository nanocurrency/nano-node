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
class frontier_store_partial : public frontier_store
{
private:
	nano::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	explicit frontier_store_partial (nano::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void put (nano::write_transaction const & transaction_a, nano::block_hash const & block_a, nano::account const & account_a) override
	{
		nano::db_val<Val> account (account_a);
		auto status (store.put (transaction_a, tables::frontiers, block_a, account));
		release_assert_success (store, status);
	}

	nano::account get (nano::transaction const & transaction_a, nano::block_hash const & block_a) const override
	{
		nano::db_val<Val> value;
		auto status (store.get (transaction_a, tables::frontiers, nano::db_val<Val> (block_a), value));
		release_assert (store.success (status) || store.not_found (status));
		nano::account result{};
		if (store.success (status))
		{
			result = static_cast<nano::account> (value);
		}
		return result;
	}

	void del (nano::write_transaction const & transaction_a, nano::block_hash const & block_a) override
	{
		auto status (store.del (transaction_a, tables::frontiers, block_a));
		release_assert_success (store, status);
	}

	nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const & transaction_a) const override
	{
		return store.template make_iterator<nano::block_hash, nano::account> (transaction_a, tables::frontiers);
	}

	nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const & transaction_a, nano::block_hash const & hash_a) const override
	{
		return store.template make_iterator<nano::block_hash, nano::account> (transaction_a, tables::frontiers, nano::db_val<Val> (hash_a));
	}

	nano::store_iterator<nano::block_hash, nano::account> end () const override
	{
		return nano::store_iterator<nano::block_hash, nano::account> (nullptr);
	}

	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const override
	{
		parallel_traversal<nano::uint256_t> (
		[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, start), !is_last ? this->begin (transaction, end) : this->end ());
		});
	}
};

}
