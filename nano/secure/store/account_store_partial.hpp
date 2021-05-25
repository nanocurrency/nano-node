#pragma once

#include <nano/secure/blockstore_partial.hpp>

namespace nano
{
template <typename Val, typename Derived_Store>
class block_store_partial;

template <typename Val, typename Derived_Store>
void release_assert_success (block_store_partial<Val, Derived_Store> const & block_store, const int status);

template <typename Val, typename Derived_Store>
class account_store_partial : public account_store
{
private:
	nano::block_store_partial<Val, Derived_Store> & block_store;

	friend void release_assert_success<Val, Derived_Store> (block_store_partial<Val, Derived_Store> const & block_store, const int status);

public:
	explicit account_store_partial (nano::block_store_partial<Val, Derived_Store> & block_store_a) :
		block_store (block_store_a){};

	void account_put (nano::write_transaction const & transaction_a, nano::account const & account_a, nano::account_info const & info_a) override
	{
		// Check we are still in sync with other tables
		nano::db_val<Val> info (info_a);
		auto status = block_store.put (transaction_a, tables::accounts, account_a, info);
		release_assert_success (block_store, status);
	}

	bool account_get (nano::transaction const & transaction_a, nano::account const & account_a, nano::account_info & info_a) override
	{
		nano::db_val<Val> value;
		nano::db_val<Val> account (account_a);
		auto status1 (block_store.get (transaction_a, tables::accounts, account, value));
		release_assert (block_store.success (status1) || block_store.not_found (status1));
		bool result (true);
		if (block_store.success (status1))
		{
			nano::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = info_a.deserialize (stream);
		}
		return result;
	}

	void account_del (nano::write_transaction const & transaction_a, nano::account const & account_a) override
	{
		auto status = block_store.del (transaction_a, tables::accounts, account_a);
		release_assert_success (block_store, status);
	}

	bool exists (nano::transaction const & transaction_a, nano::account const & account_a) override
	{
		auto iterator (accounts_begin (transaction_a, account_a));
		return iterator != accounts_end () && nano::account (iterator->first) == account_a;
	}

	size_t account_count (nano::transaction const & transaction_a) override
	{
		return block_store.count (transaction_a, tables::accounts);
	}

	nano::store_iterator<nano::account, nano::account_info> accounts_begin (nano::transaction const & transaction_a, nano::account const & account_a) const override
	{
		return block_store.template make_iterator<nano::account, nano::account_info> (transaction_a, tables::accounts, nano::db_val<Val> (account_a));
	}

	nano::store_iterator<nano::account, nano::account_info> accounts_begin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<nano::account, nano::account_info> (transaction_a, tables::accounts);
	}

	nano::store_iterator<nano::account, nano::account_info> accounts_rbegin (nano::transaction const & transaction_a) const override
	{
		return block_store.template make_iterator<nano::account, nano::account_info> (transaction_a, tables::accounts, false);
	}

	nano::store_iterator<nano::account, nano::account_info> accounts_end () const override
	{
		return nano::store_iterator<nano::account, nano::account_info> (nullptr);
	}

	void accounts_for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::account_info>, nano::store_iterator<nano::account, nano::account_info>)> const & action_a) const override
	{
		parallel_traversal<nano::uint256_t> (
		[&action_a, this] (nano::uint256_t const & start, nano::uint256_t const & end, bool const is_last) {
			auto transaction (this->block_store.tx_begin_read ());
			action_a (transaction, this->accounts_begin (transaction, start), !is_last ? this->accounts_begin (transaction, end) : this->accounts_end ());
		});
	}

};

}
