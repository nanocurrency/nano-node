#pragma once

#include <celerix/lib/numbers.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/typed_iterator.hpp>

#include <cstdint>
#include <functional>

namespace celerix
{
// class account;
}
namespace celerix::store
{
/**
 * A lookup table of all representatives and their vote weight
 */
class rep_weight
{
public:
	using iterator = typed_iterator<celerix::account, celerix::uint128_union>;

public:
	virtual ~rep_weight (){};
	virtual uint64_t count (store::transaction const & txn_a) = 0;
	virtual celerix::uint128_t get (store::transaction const & txn_a, celerix::account const & representative_a) = 0;
	virtual void put (store::write_transaction const & txn_a, celerix::account const & representative_a, celerix::uint128_t const & weight_a) = 0;
	virtual void del (store::write_transaction const &, celerix::account const & representative_a) = 0;
	virtual iterator begin (store::transaction const & transaction_a, celerix::account const & representative_a) const = 0;
	virtual iterator begin (store::transaction const & transaction_a) const = 0;
	virtual iterator end (store::transaction const & transaction_a) const = 0;
	virtual void for_each_par (std::function<void (store::read_transaction const &, iterator, iterator)> const & action_a) const = 0;
};
}
