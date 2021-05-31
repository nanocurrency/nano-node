#pragma once

#include <nano/secure/blockstore_partial.hpp>

namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action);
}

namespace nano
{
template <typename Val, typename Derived_Store>
class block_store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (block_store_partial<Val, Derived_Store> const & block_store, const int status);

template <typename Val, typename Derived_Store>
class peer_store_partial : public peer_store
{
private:
	nano::block_store_partial<Val, Derived_Store> & block_store;

	friend void release_assert_success<Val, Derived_Store> (block_store_partial<Val, Derived_Store> const & block_store, const int status);

public:
	explicit peer_store_partial (nano::block_store_partial<Val, Derived_Store> & block_store_a) :
		block_store (block_store_a){};

	void put (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override
	{
		auto status = block_store.put_key (transaction_a, tables::peers, endpoint_a);
		release_assert_success (block_store, status);
	}

	void del (nano::write_transaction const & transaction_a, nano::endpoint_key const & endpoint_a) override
	{
		auto status (block_store.del (transaction_a, tables::peers, endpoint_a));
		release_assert_success (block_store, status);
	}

	bool exists (nano::transaction const & transaction_a, nano::endpoint_key const & endpoint_a) const override
	{
		return block_store.exists (transaction_a, tables::peers, nano::db_val<Val> (endpoint_a));
	}

	size_t count (nano::transaction const & transaction_a) const override
	{
		return block_store.count (transaction_a, tables::peers);
	}

	void clear (nano::write_transaction const & transaction_a) override
	{
		auto status = block_store.drop (transaction_a, tables::peers);
		release_assert_success (block_store, status);
	}

	nano::store_iterator<nano::endpoint_key, nano::no_value> begin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<nano::endpoint_key, nano::no_value> (transaction_a, tables::peers);
	}

	nano::store_iterator<nano::endpoint_key, nano::no_value> end () const override
	{
		return nano::store_iterator<nano::endpoint_key, nano::no_value> (nullptr);
	}
};

}
