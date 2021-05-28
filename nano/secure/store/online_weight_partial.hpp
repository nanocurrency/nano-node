#pragma once

#include <nano/secure/blockstore_partial.hpp>

namespace nano
{
template <typename Val, typename Derived_Store>
class block_store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (block_store_partial<Val, Derived_Store> const & block_store, const int status);

template <typename Val, typename Derived_Store>
class online_weight_store_partial : public online_weight_store
{
private:
	nano::block_store_partial<Val, Derived_Store> & block_store;

	friend void release_assert_success<Val, Derived_Store> (block_store_partial<Val, Derived_Store> const & block_store, const int status);

public:
	explicit online_weight_store_partial (nano::block_store_partial<Val, Derived_Store> & block_store_a) :
		block_store (block_store_a){};

	void online_weight_put (nano::write_transaction const & transaction_a, uint64_t time_a, nano::amount const & amount_a) override
	{
		nano::db_val<Val> value (amount_a);
		auto status (block_store.put (transaction_a, tables::online_weight, time_a, value));
		release_assert_success (block_store, status);
	}

	void online_weight_del (nano::write_transaction const & transaction_a, uint64_t time_a) override
	{
		auto status (block_store.del (transaction_a, tables::online_weight, time_a));
		release_assert_success (block_store, status);
	}

	nano::store_iterator<uint64_t, nano::amount> online_weight_begin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<uint64_t, nano::amount> (transaction_a, tables::online_weight);
	}

	nano::store_iterator<uint64_t, nano::amount> online_weight_rbegin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<uint64_t, nano::amount> (transaction_a, tables::online_weight, false);
	}

	nano::store_iterator<uint64_t, nano::amount> online_weight_end () const override
	{
		return nano::store_iterator<uint64_t, nano::amount> (nullptr);
	}

	size_t online_weight_count (nano::transaction const & transaction_a) const override
	{
		return block_store.count (transaction_a, tables::online_weight);
	}

	void online_weight_clear (nano::write_transaction const & transaction_a) override
	{
		auto status (block_store.drop (transaction_a, tables::online_weight));
		release_assert_success (block_store, status);
	}
};

}
