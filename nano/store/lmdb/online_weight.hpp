#pragma once

#include <celerix/store/online_weight.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace celerix::store::lmdb
{
class online_weight : public celerix::store::online_weight
{
private:
	celerix::store::lmdb::component & store;

public:
	explicit online_weight (celerix::store::lmdb::component & store_a);
	void put (store::write_transaction const & transaction_a, uint64_t time_a, celerix::amount const & amount_a) override;
	void del (store::write_transaction const & transaction_a, uint64_t time_a) override;
	iterator begin (store::transaction const & transaction_a) const override;
	iterator end (store::transaction const & transaction_a) const override;
	size_t count (store::transaction const & transaction_a) const override;
	void clear (store::write_transaction const & transaction_a) override;

	/**
	 * Samples of online vote weight
	 * uint64_t -> celerix::amount
	 */
	MDB_dbi online_weight_handle{ 0 };
};
} // namespace celerix::store::lmdb
