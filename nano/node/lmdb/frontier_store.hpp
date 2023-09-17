#pragma once

#include <nano/store/component.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano
{
namespace lmdb
{
	class store;
	class frontier_store : public nano::frontier_store
	{
	private:
		nano::lmdb::store & store;

	public:
		frontier_store (nano::lmdb::store & store);
		void put (nano::write_transaction const &, nano::block_hash const &, nano::account const &) override;
		nano::account get (nano::transaction const &, nano::block_hash const &) const override;
		void del (nano::write_transaction const &, nano::block_hash const &) override;
		nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &) const override;
		nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &, nano::block_hash const &) const override;
		nano::store_iterator<nano::block_hash, nano::account> end () const override;
		void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const override;

		/**
		 * Maps head block to owning account
		 * nano::block_hash -> nano::account
		 */
		MDB_dbi frontiers_handle{ 0 };
	};
}
}
