#pragma once

#include <nano/secure/store.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class account_store : public nano::account_store
	{
	private:
		nano::lmdb::store & store;

	public:
		explicit account_store (nano::lmdb::store & store_a);
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

		/**
		 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
		 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
		 */
		MDB_dbi accounts_v0_handle{ 0 };

		/**
		 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
		 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t
		 */
		MDB_dbi accounts_v1_handle{ 0 };

		/**
		 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch
		 * nano::account -> nano::block_hash, nano::block_hash, nano::block_hash, nano::amount, uint64_t, uint64_t, nano::epoch
		 */
		MDB_dbi accounts_handle{ 0 };

		/**
		 * Representative weights. (Removed)
		 * nano::account -> nano::uint128_t
		 */
		MDB_dbi representation_handle{ 0 };
	};
}
}
