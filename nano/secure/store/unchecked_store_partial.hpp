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
void release_assert_success (store_partial<Val, Derived_Store> const &, int const);

template <typename Val, typename Derived_Store>
class unchecked_store_partial : public unchecked_store
{
private:
	nano::store_partial<Val, Derived_Store> & store;

	friend void release_assert_success<Val, Derived_Store> (store_partial<Val, Derived_Store> const &, int const);

public:
	unchecked_store_partial (nano::store_partial<Val, Derived_Store> & store_a) :
		store (store_a){};

	void clear (nano::write_transaction const & transaction_a) override
	{
		auto status = store.drop (transaction_a, tables::unchecked);
		release_assert_success (store, status);
	}

	void put (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a, nano::unchecked_info const & info_a) override
	{
		if (get (transaction_a, key_a.previous).size () > 1)
			return;
		nano::db_val<Val> info (info_a);
		auto status (store.put (transaction_a, tables::unchecked, key_a, info));
		release_assert_success (store, status);
	}

	void put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a) override
	{
		nano::unchecked_key key (hash_a, block_a->hash ());
		nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
		put (transaction_a, key, info);
	}

	bool exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) override
	{
		nano::db_val<Val> value;
		auto status (store.get (transaction_a, tables::unchecked, nano::db_val<Val> (unchecked_key_a), value));
		release_assert (store.success (status) || store.not_found (status));
		return (store.success (status));
	}

	void del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a) override
	{
		auto status (store.del (transaction_a, tables::unchecked, key_a));
		release_assert_success (store, status);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> end () const override
	{
		return nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a) const override
	{
		return store.template make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const override
	{
		return store.template make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked, nano::db_val<Val> (key_a));
	}

	size_t count (nano::transaction const & transaction_a) override
	{
		return store.count (transaction_a, tables::unchecked);
	}

	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const override
	{
		parallel_traversal<nano::uint512_t> (
		[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
			nano::unchecked_key key_start (start);
			nano::unchecked_key key_end (end);
			auto transaction (this->store.tx_begin_read ());
			action_a (transaction, this->begin (transaction, key_start), !is_last ? this->begin (transaction, key_end) : this->end ());
		});
	}
};

}
