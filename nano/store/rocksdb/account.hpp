#pragma once

#include <nano/store/account.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class account : public nano::store::account
{
private:
	nano::store::rocksdb::component & store;

public:
	explicit account (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction, nano::account const & account, nano::account_info const & info) override;
	bool get (store::transaction const & transaction_a, nano::account const & account_a, nano::account_info & info_a) override;
	void del (store::write_transaction const & transaction_a, nano::account const & account_a) override;
	bool exists (store::transaction const & transaction_a, nano::account const & account_a) override;
	size_t count (store::transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, nano::account const & account_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator rbegin (store::transaction const & transaction_a) const override;
	iterator end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace nano::store::rocksdb
