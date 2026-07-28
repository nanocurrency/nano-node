#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>

namespace nano
{
class account_receivable_by_amount_key final
{
public:
	account_receivable_by_amount_key () = default;
	account_receivable_by_amount_key (nano::account const &, nano::amount const &, nano::block_hash const &);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

	bool operator== (nano::account_receivable_by_amount_key const &) const = default;
	auto operator<=> (nano::account_receivable_by_amount_key const &) const = default;

	nano::account account{};
	nano::amount amount{ 0 };
	nano::block_hash send_block_hash{ 0 };
};
}
