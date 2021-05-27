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
class unchecked_store_partial : public unchecked_store
{
private:
	nano::block_store_partial<Val, Derived_Store> & block_store;

	friend void release_assert_success<Val, Derived_Store> (block_store_partial<Val, Derived_Store> const & block_store, const int status);

public:
	explicit unchecked_store_partial (nano::block_store_partial<Val, Derived_Store> & block_store_a) :
		block_store (block_store_a){};

	void unchecked_clear (nano::write_transaction const & transaction_a) override
	{
		auto status = block_store.drop (transaction_a, tables::unchecked);
		release_assert_success (*this, status);
	}

	void unchecked_put (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a, nano::unchecked_info const & info_a) override
	{
		nano::db_val<Val> info (info_a);
		auto status (put (transaction_a, tables::unchecked, key_a, info));
		release_assert_success (*this, status);
	}

	void unchecked_put (nano::write_transaction const & transaction_a, nano::block_hash const & hash_a, std::shared_ptr<nano::block> const & block_a) override
	{
		nano::unchecked_key key (hash_a, block_a->hash ());
		nano::unchecked_info info (block_a, block_a->account (), nano::seconds_since_epoch (), nano::signature_verification::unknown);
		unchecked_put (transaction_a, key, info);
	}

	bool unchecked_exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) override
	{
		nano::db_val<Val> value;
		auto status (get (transaction_a, tables::unchecked, nano::db_val<Val> (unchecked_key_a), value));
		release_assert (success (status) || not_found (status));
		return (success (status));
	}

	void unchecked_del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a) override
	{
		auto status (block_store.del (transaction_a, tables::unchecked, key_a));
		release_assert_success (*this, status);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_end () const override
	{
		return nano::store_iterator<nano::unchecked_key, nano::unchecked_info> (nullptr);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked);
	}

	nano::store_iterator<nano::unchecked_key, nano::unchecked_info> unchecked_begin (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const override
	{
		return make_iterator<nano::unchecked_key, nano::unchecked_info> (transaction_a, tables::unchecked, nano::db_val<Val> (key_a));
	}

	size_t unchecked_count (nano::transaction const & transaction_a) override
	{
		return block_store.count (transaction_a, tables::unchecked);
	}

	void unchecked_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>, nano::store_iterator<nano::unchecked_key, nano::unchecked_info>)> const & action_a) const override
	{
		parallel_traversal<nano::uint512_t> (
		[&action_a, this] (nano::uint512_t const & start, nano::uint512_t const & end, bool const is_last) {
			nano::unchecked_key key_start (start);
			nano::unchecked_key key_end (end);
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->unchecked_begin (transaction, key_start), !is_last ? this->unchecked_begin (transaction, key_end) : this->unchecked_end ());
		});
	}
};

}
