#pragma once

#include <nano/secure/store.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class unchecked_store : public nano::unchecked_store
	{
	private:
		nano::lmdb::store & store;

	public:
		unchecked_store (nano::lmdb::store & store_a);

		void clear (nano::write_transaction const & transaction_a) override;
		void put (nano::write_transaction const & transaction_a, nano::hash_or_account const & dependency, nano::unchecked_info const & info_a) override;
		bool exists (nano::transaction const & transaction_a, nano::unchecked_key const & unchecked_key_a) override;
		void del (nano::write_transaction const & transaction_a, nano::unchecked_key const & key_a) override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> end () const override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> begin (nano::transaction const & transaction_a) const override;
		nano::store_iterator<nano::unchecked_key, nano::unchecked_info> lower_bound (nano::transaction const & transaction_a, nano::unchecked_key const & key_a) const override;
		size_t count (nano::transaction const & transaction_a) override;

		/**
		 * Unchecked bootstrap blocks info.
		 * nano::block_hash -> nano::unchecked_info
		 */
		MDB_dbi unchecked_handle{ 0 };
	};
}
}
