#pragma once

#include <nano/store/frontier.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace nano::store::lmdb
{
class component;
}
namespace nano::store::lmdb
{
class frontier : public nano::store::frontier
{
private:
	nano::store::lmdb::component & store;

public:
	frontier (nano::store::lmdb::component & store);
	void put (store::write_transaction const &, nano::block_hash const &, nano::account const &) override;
	nano::account get (store::transaction const &, nano::block_hash const &) const override;
	void del (store::write_transaction const &, nano::block_hash const &) override;
	store::iterator<nano::block_hash, nano::account> begin (store::transaction const &) const override;
	store::iterator<nano::block_hash, nano::account> begin (store::transaction const &, nano::block_hash const &) const override;
	store::iterator<nano::block_hash, nano::account> end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::block_hash, nano::account>, store::iterator<nano::block_hash, nano::account>)> const & action_a) const override;

	/**
	 * Maps head block to owning account
	 * nano::block_hash -> nano::account
	 */
	MDB_dbi frontiers_handle{ 0 };
};
} // namespace nano::store::lmdb
