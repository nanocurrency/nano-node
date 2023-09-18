#pragma once

#include <nano/store/confirmation_height.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class confirmation_height_store : public nano::confirmation_height_store
	{
		nano::lmdb::store & store;

	public:
		explicit confirmation_height_store (nano::lmdb::store & store_a);
		void put (nano::write_transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a) override;
		bool get (nano::transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info & confirmation_height_info_a) override;
		bool exists (nano::transaction const & transaction_a, nano::account const & account_a) const override;
		void del (nano::write_transaction const & transaction_a, nano::account const & account_a) override;
		uint64_t count (nano::transaction const & transaction_a) override;
		void clear (nano::write_transaction const & transaction_a, nano::account const & account_a) override;
		void clear (nano::write_transaction const & transaction_a) override;
		nano::store_iterator<nano::account, nano::confirmation_height_info> begin (nano::transaction const & transaction_a, nano::account const & account_a) const override;
		nano::store_iterator<nano::account, nano::confirmation_height_info> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::account, nano::confirmation_height_info> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::account, nano::confirmation_height_info>, nano::store_iterator<nano::account, nano::confirmation_height_info>)> const & action_a) const override;

		/*
		 * Confirmation height of an account, and the hash for the block at that height
		 * nano::account -> uint64_t, nano::block_hash
		 */
		MDB_dbi confirmation_height_handle{ 0 };
	};
}
}
