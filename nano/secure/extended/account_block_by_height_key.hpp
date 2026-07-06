#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/stream.hpp>

namespace nano
{
class account_block_by_height_key final
{
public:
	account_block_by_height_key () = default;
	account_block_by_height_key (nano::account const &, uint64_t);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

	bool operator== (nano::account_block_by_height_key const &) const = default;
	auto operator<=> (nano::account_block_by_height_key const &) const = default;

	nano::account account{};
	uint64_t height{ 0 };
};
}
