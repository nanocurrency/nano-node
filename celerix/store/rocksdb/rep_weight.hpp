#pragma once

#include <celerix/store/rep_weight.hpp>

namespace celerix::store::rocksdb
{
class component;
}
namespace celerix::store::rocksdb
{
class rep_weight : public celerix::store::rep_weight
{
private:
	celerix::store::rocksdb::component & store;

public:
	explicit rep_weight (celerix::store::rocksdb::component & store_a);
	uint64_t count (store::transaction const & txn_a) override;
	celerix::uint128_t get (store::transaction const & txn_a, celerix::account const & representative_a) override;
	void put (store::write_transaction const & txn_a, celerix::account const & representative_a, celerix::uint128_t const & weight_a) override;
	void del (store::write_transaction const &, celerix::account const & representative_a) override;
	iterator begin (store::transaction const & txn_a, celerix::account const & representative_a) const override;
	iterator begin (store::transaction const & txn_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;
};
}
