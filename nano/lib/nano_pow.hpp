#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/streams.hpp>

#include <array>

namespace nano
{
class nano_pow final
{
public:
	static constexpr uint64_t proof_size = 12;
	nano_pow () = default;
	nano_pow (nano::legacy_pow pow_a); // Temporary while dealing with transition to new nano pow
	nano_pow (std::array<uint8_t, proof_size> const & bytes_a);
	nano_pow (uint96_t pow);
	uint96_t number () const;
	nano::legacy_pow as_legacy () const;

	nano_pow & operator++ ();
	bool operator== (nano::nano_pow const & nano_pow) const;

	// Stored as big endian for easier (de)serialization
	std::array<uint8_t, proof_size> bytes;
};

std::string to_string_hex (nano::nano_pow const & value_a);
}
std::istream & operator>> (std::istream & input, nano::nano_pow & pow);
std::ostream & operator<< (std::ostream & output, nano::nano_pow const & num);
