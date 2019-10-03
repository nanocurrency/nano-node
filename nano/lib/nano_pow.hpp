#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/streams.hpp>

#include <array>

namespace nano
{
class nano_pow final
{
public:
	static const uint64_t size = 12; // Number of bytes which are serialized

	using num_type = uint96_t;
	using real_byte_type = std::array<uint8_t, size>;

	nano_pow () = default;
	nano_pow (nano::legacy_pow pow_a); // Temporary while dealing with transition to new nano pow
	nano_pow (real_byte_type const & bytes_a);
	nano_pow (num_type pow);
	num_type number () const;
	nano::legacy_pow as_legacy () const;

	bool operator== (nano::nano_pow const & nano_pow) const;

	// Stored as big endian for easier (de)serialization
	std::array<uint8_t, 16> bytes;
};

bool try_read (nano::stream & stream_a, nano::nano_pow & value_a);
void write (nano::stream & stream_a, nano::nano_pow const & value_a);
bool from_string_hex (std::string const & value_a, nano::nano_pow & target_a);
std::string to_string_hex (nano::nano_pow const & value_a);
}
