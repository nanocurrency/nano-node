#pragma once

#include <celerix/store/rep_weight.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class component;

class rep_weight : public celerix::store::rep_weight
{
private:
	celerix::store::lmdb::component & store;

public:
	explicit rep_weight (celerix::store::lmdb::component & store_a);

	uint64_t count (store::transaction const & txn) override;
	celerix::uint128_t get (store::transaction const & txn_a, celerix::account const & representative_a) override;
	void put (store::write_transaction const & txn_a, celerix::account const & representative_a, celerix::uint128_t const & weight_a) override;
	void del (store::write_transaction const &, celerix::account const & representative_a) override;
	iterator begin (store::transaction const & transaction_a, celerix::account const & representative_a) const override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const override;

	/**
	 * Representative weights
	 * celerix::account -> uint128_t
	 */
	MDB_dbi rep_weights_handle{ 0 };
};
}
