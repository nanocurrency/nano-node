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
	std::vector <epoch_info> epochs_m;
};
}
