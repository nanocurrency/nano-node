#pragma once

#include <nano/secure/store.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class pending_store : public nano::pending_store
	{
	private:
		nano::lmdb::store & store;

	public:
		explicit pending_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info const & pending_info_a) override;
		void del (nano::write_transaction const & transaction_a, nano::pending_key const & key_a) override;
		bool get (nano::transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info & pending_a) override;
		bool exists (nano::transaction const & transaction_a, nano::pending_key const & key_a) override;
		bool any (nano::transaction const & transaction_a, nano::account const & account_a) override;
		nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const & transaction_a, nano::pending_key const & key_a) const override;
		nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::pending_key, nano::pending_info> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::pending_key, nano::pending_info>, nano::store_iterator<nano::pending_key, nano::pending_info>)> const & action_a) const override;

		/**
		 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
		 * nano::account, nano::block_hash -> nano::account, nano::amount
		 */
		MDB_dbi pending_v0_handle{ 0 };

		/**
		 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
		 * nano::account, nano::block_hash -> nano::account, nano::amount
		 */
		MDB_dbi pending_v1_handle{ 0 };

		/**
		 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
		 * nano::account, nano::block_hash -> nano::account, nano::amount, nano::epoch
		 */
		MDB_dbi pending_handle{ 0 };
	};
}
}
