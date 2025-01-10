#pragma once

#include <celerix/store/online_weight.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class online_weight : public celerix::store::online_weight
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit online_weight (celerix::store::rocksdb::component & store_a);
	void put (store::write_transaction const & transaction_a, uint64_t time_a, celerix::amount const & amount_a) override;
	void del (store::write_transaction const & transaction_a, uint64_t time_a) override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;
};
} // namespace celerix::store::rocksdb
