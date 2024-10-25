#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/store/component.hpp>
#include <nano/store/iterator.hpp>

#include <cstdint>
#include <functional>

namespace nano
{
// class account;
}
namespace nano::store
{
/**
 * A lookup table of all representatives and their vote weight
 */
class rep_weight
{
public:
	using iterator = store::iterator<nano::account, nano::uint128_union>;

public:
	virtual ~rep_weight (){};
	virtual uint64_t count (store::transaction const & txn_a) = 0;
	virtual nano::uint128_t get (store::transaction const & txn_a, nano::account const & representative_a) = 0;
	virtual void put (store::write_transaction const & txn_a, nano::account const & representative_a, nano::uint128_t const & weight_a) = 0;
	virtual void del (store::write_transaction const &, nano::account const & representative_a) = 0;
	virtual iterator begin (store::transaction const & transaction_a, nano::account const & representative_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
}
