#pragma once

#include <nano/store/rep_weight.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>
#include <lmdbxx/lmdb++.h>

namespace nano::store::lmdb
{
class component;

class rep_weight : public nano::store::rep_weight
{
private:
	nano::store::lmdb::component & store;

public:
	explicit rep_weight (nano::store::lmdb::component & store_a);

	uint64_t count (store::transaction const & txn) override;
	nano::uint128_t get (store::transaction const & txn_a, nano::account const & representative_a) override;
	void put (store::write_transaction const & txn_a, nano::account const & representative_a, nano::uint128_t const & weight_a) override;
	void del (store::write_transaction const &, nano::account const & representative_a) override;
	store::iterator<nano::account, nano::uint128_union> begin (store::transaction const & transaction_a, nano::account const & representative_a) const override;
	store::iterator<nano::account, nano::uint128_union> begin (store::transaction const & transaction_a) const override;
	store::iterator<nano::account, nano::uint128_union> end () const override;
	void for_each_par (std::function<void (store::read_transaction const &, store::iterator<nano::account, nano::uint128_union>, store::iterator<nano::account, nano::uint128_union>)> const & action_a) const override;

	/**
	 * Representative weights
	 * nano::account -> uint128_t
	 */
	::lmdb::dbi rep_weights_handle;
};
}
