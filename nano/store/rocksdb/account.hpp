#pragma once

#include <nano/store/account.hpp>

namespace nano
{
namespace rocksdb
{
	class store;
	class account_store : public nano::account_store
	{
	private:
		nano::rocksdb::store & store;

	public:
		explicit account_store (nano::rocksdb::store & store_a);
		void put (nano::write_transaction const & transaction, nano::account const & account, nano::account_info const & info) override;
		bool get (nano::transaction const & transaction_a, nano::account const & account_a, nano::account_info & info_a) override;
		void del (nano::write_transaction const & transaction_a, nano::account const & account_a) override;
		bool exists (nano::transaction const & transaction_a, nano::account const & account_a) override;
		size_t count (nano::transaction const & transaction_a) override;
		nano::store_iterator<nano::account, nano::account_info> begin (nano::transaction const & transaction_a, nano::account const & account_a) const override;
		nano::store_iterator<nano::account, nano::account_info> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::account, nano::account_info> rbegin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::account, nano::account_info> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::account_info>, nano::store_iterator<nano::account, nano::account_info>)> const & action_a) const override;
	};
}
}
