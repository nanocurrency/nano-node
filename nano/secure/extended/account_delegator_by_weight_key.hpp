#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>

namespace nano
{
class account_delegator_by_weight_key final
{
public:
	account_delegator_by_weight_key () = default;
	account_delegator_by_weight_key (nano::account const &, nano::amount const &, nano::account const &);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

	bool operator== (nano::account_delegator_by_weight_key const &) const = default;
	auto operator<=> (nano::account_delegator_by_weight_key const &) const = default;

	nano::account representative{};
	nano::amount weight{ 0 };
	nano::account delegator{};
};
}
