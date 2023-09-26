#pragma once

#include <nano/store/confirmation_height.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class component;
}
namespace nano::store::lmdb
{
class confirmation_height : public nano::store::confirmation_height
{
	nano::store::lmdb::component & store;

public:
	explicit confirmation_height (nano::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a) override;
	bool get (store::transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info & confirmation_height_info_a) override;
	bool exists (store::transaction const & transaction_a, nano::account const & account_a) const override;
	void del (store::write_transaction const & transaction_a, nano::account const & account_a) override;
	uint64_t count (store::transaction const & transaction_a) override;
	void clear (store::write_transaction const & transaction_a, nano::account const & account_a) override;
	void clear (store::write_transaction const & transaction_a) override;
	store::iterator<nano::account, nano::confirmation_height_info> begin (store::transaction const & transaction_a, nano::account const & account_a) const override;
	store::iterator<nano::account, nano::confirmation_height_info> begin (store::transaction const & transaction_a) const override;
	store::iterator<nano::account, nano::confirmation_height_info> end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::account, nano::confirmation_height_info>, store::iterator<nano::account, nano::confirmation_height_info>)> const & action_a) const override;

	/*
		 * Confirmation height of an account, and the hash for the block at that height
		 * nano::account -> uint64_t, nano::block_hash
		 */
	MDB_dbi confirmation_height_handle{ 0 };
};
} // namespace nano::store::lmdb
