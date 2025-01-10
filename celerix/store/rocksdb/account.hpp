#pragma once

#include <celerix/store/account.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class account : public celerix::store::account
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit account (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction, celerix::account const & account, celerix::account_info const & info) override;
	bool get (store::transaction const & transaction_a, celerix::account const & account_a, celerix::account_info & info_a) override;
	void del (store::write_transaction const & transaction_a, celerix::account const & account_a) override;
	bool exists (store::transaction const & transaction_a, celerix::account const & account_a) override;
	size_t count (store::transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::account const & account_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace celerix::store::rocksdb
