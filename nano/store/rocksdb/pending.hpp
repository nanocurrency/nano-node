#pragma once

#include <nano/store/pending.hpp>

namespace nano::store::rocksdb
{
class pending : public nano::store::pending
{
private:
	nano::store::rocksdb::component & store;

public:
	explicit pending (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::pending_key const & key_a, nano::pending_info const & pending_info_a) override;
	void del (store::write_transaction const & transaction_a, nano::pending_key const & key_a) override;
	std::optional<nano::pending_info> get (store::transaction const & transaction_a, nano::pending_key const & key_a) override;
	bool exists (store::transaction const & transaction_a, nano::pending_key const & key_a) override;
	bool any (store::transaction const & transaction_a, nano::account const & account_a) override;
	iterator begin (store::transaction const & transaction_a, nano::pending_key const & key_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace nano::store::rocksdb
