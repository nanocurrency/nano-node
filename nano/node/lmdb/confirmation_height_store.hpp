#pragma once

#include <nano/secure/store.hpp>

namespace nano
{
class mdb_store;
class confirmation_height_store_mdb : public confirmation_height_store
{
	nano::mdb_store & store;

public:
	explicit confirmation_height_store_mdb (nano::mdb_store & store_a);
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
};
}