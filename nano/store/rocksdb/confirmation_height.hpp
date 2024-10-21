#pragma once

#include <nano/store/confirmation_height.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class confirmation_height : public nano::store::confirmation_height
{
	nano::store::rocksdb::component & store;

public:
	explicit confirmation_height (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a) override;
	bool get (store::transaction const & transaction_a, nano::account const & account_a, nano::confirmation_height_info & confirmation_height_info_a) override;
	bool exists (store::transaction const & transaction_a, nano::account const & account_a) const override;
	void del (store::write_transaction const & transaction_a, nano::account const & account_a) override;
	uint64_t count (store::transaction const & transaction_a) override;
	void clear (store::write_transaction const & transaction_a, nano::account const & account_a) override;
	void clear (store::write_transaction const & transaction_a) override;
	iterator begin (store::transaction const & transaction_a, nano::account const & account_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
} // namespace nano::store::rocksdb
