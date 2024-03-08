#pragma once

#include <nano/lib/stream.hpp>

#include <cstdint>
#include <string>

namespace nano
{
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};

std::string_view to_string (block_type);
/**
 * Serialize block type as an 8-bit value
 */
void serialize_block_type (nano::stream &, nano::block_type const &);
} // namespace nano
