#pragma once

#include <celerix/store/confirmation_height.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class confirmation_height : public celerix::store::confirmation_height
{
	celerix::store::rocksdb::component & store;

public:
	explicit confirmation_height (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, celerix::account const & account_a, celerix::confirmation_height_info const & confirmation_height_info_a) override;
	bool get (store::transaction const & transaction_a, celerix::account const & account_a, celerix::confirmation_height_info & confirmation_height_info_a) override;
	bool exists (store::transaction const & transaction_a, celerix::account const & account_a) const override;
	void del (store::write_transaction const & transaction_a, celerix::account const & account_a) override;
	uint64_t count (store::transaction const & transaction_a) override;
	void clear (store::write_transaction const & transaction_a, celerix::account const & account_a) override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::account const & account_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace celerix::store::rocksdb
