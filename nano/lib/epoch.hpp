#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

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
	epoch_2 = 4,
	max = epoch_2
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<nano::epoch> normalized_epoch (nano::epoch epoch_a);
}
namespace std
{
template <>
struct hash<::nano::epoch>
{
	std::size_t operator() (::nano::epoch const & epoch_a) const
	{
		return std::underlying_type_t<::nano::epoch> (epoch_a);
	}
};
}
