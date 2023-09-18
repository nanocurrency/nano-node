#pragma once

#include <nano/store/online_weight.hpp>

namespace nano::store::rocksdb
{
class component;
}
namespace nano::store::rocksdb
{
class online_weight : public nano::store::online_weight
{
private:
	nano::store::rocksdb::component & store;

public:
	explicit online_weight (nano::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, uint64_t time_a, nano::amount const & amount_a) override;
	void del (store::write_transaction const & transaction_a, uint64_t time_a) override;
	store::iterator<uint64_t, nano::amount> begin (store::transaction const & transaction_a) const override;
	store::iterator<uint64_t, nano::amount> rbegin (store::transaction const & transaction_a) const override;
	store::iterator<uint64_t, nano::amount> end () const override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
};
} // namespace nano::store::rocksdb
