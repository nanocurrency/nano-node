#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>

namespace nano
{
class account_receivable_by_amount_info final
{
public:
	account_receivable_by_amount_info () = default;
	account_receivable_by_amount_info (nano::account const &, nano::epoch);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	size_t db_size () const;

	bool operator== (nano::account_receivable_by_amount_info const &) const = default;

	nano::account source{};
	nano::epoch epoch{ nano::epoch::epoch_0 };
};
}
