#pragma once

#include <nano/lib/numbers.hpp>

namespace nano
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_end
};
class epoch_info
{
public:
	nano::public_key signer;
	nano::uint256_union link;
};
class epochs
{
public:
	bool is_epoch_link (nano::uint256_union const & link_a);
	nano::uint256_union link (nano::epoch epoch_a) const;
	nano::public_key signer (nano::uint256_union const & link_a) const;
	void add (nano::epoch epoch_a, nano::public_key const & signer_a, nano::uint256_union const & link_a);
private:
	nano::uint256_union link_m { 0 };
	nano::account signer_m { 0 };
};
}
