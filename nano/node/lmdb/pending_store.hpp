#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class mdb_store;
class pending_store_mdb : public pending_store
{
private:
	nano::mdb_store & store;

public:
	explicit pending_store_mdb (nano::mdb_store & store_a);
	void put (nano::write_transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info const & pending_info_a) override;
	void del (nano::write_transaction const & transaction_a, nano::pending_key const & key_a) override;
	bool get (nano::transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info & pending_a) override;
	bool exists (nano::transaction const & transaction_a, nano::pending_key const & key_a) override;
	bool any (nano::transaction const & transaction_a, nano::account const & account_a) override;
	nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const & transaction_a, nano::pending_key const & key_a) const override;
	nano::store_iterator<nano::pending_key, nano::pending_info> begin (nano::transaction const & transaction_a) const override;
	nano::store_iterator<nano::pending_key, nano::pending_info> end () const override;
	void for_each_par (std::function<void (nano::read_transaction const &, nano::store_iterator<nano::pending_key, nano::pending_info>, nano::store_iterator<nano::pending_key, nano::pending_info>)> const & action_a) const override;
};
}
