#pragma once

#include <celerix/store/pending.hpp>

namespace celerix::store::rocksdb
{
class pending : public celerix::store::pending
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit pending (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, celerix::pending_key const & key_a, celerix::pending_info const & pending_info_a) override;
	void del (store::write_transaction const & transaction_a, celerix::pending_key const & key_a) override;
	std::optional<celerix::pending_info> get (store::transaction const & transaction_a, celerix::pending_key const & key_a) override;
	bool exists (store::transaction const & transaction_a, celerix::pending_key const & key_a) override;
	bool any (store::transaction const & transaction_a, celerix::account const & account_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::pending_key const & key_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace celerix::store::rocksdb
