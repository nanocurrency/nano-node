#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class mdb_store;
class frontier_store_mdb : public frontier_store
{
public:
	frontier_store_mdb (nano::mdb_store & store);
	void put (nano::write_transaction const &, nano::block_hash const &, nano::account const &) override;
	nano::account get (nano::transaction const &, nano::block_hash const &) const override;
	void del (nano::write_transaction const &, nano::block_hash const &) override;
	nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &) const override;
	nano::store_iterator<nano::block_hash, nano::account> begin (nano::transaction const &, nano::block_hash const &) const override;
	nano::store_iterator<nano::block_hash, nano::account> end () const override;
	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::block_hash, nano::account>, nano::store_iterator<nano::block_hash, nano::account>)> const & action_a) const override;

private:
	nano::mdb_store & store;
};

}
