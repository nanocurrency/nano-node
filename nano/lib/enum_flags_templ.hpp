#pragma once

#include <nano/lib/enum_flags.hpp>

#include <ostream>
#include <sstream>
#include <string>

#include <magic_enum.hpp>

namespace nano
{
template <typename E>
std::ostream & operator<< (std::ostream & os, enum_flags<E> const & flags)
{
	using underlying_t = typename enum_flags<E>::underlying_t;
	underlying_t known_bits = 0;
	bool first = true;
	// Print names of all known set flags
	for (auto val : magic_enum::enum_values<E> ())
	{
		auto bit = static_cast<underlying_t> (val);
		if (bit == 0)
		{
			continue; // Skip the zero/none value
		}
		known_bits |= bit;
		if (flags.test (val))
		{
			if (!first)
			{
				os << ", ";
			}
			os << magic_enum::enum_name (val);
			first = false;
		}
	}
	// Print any bits not covered by known enum values (e.g. from a newer peer)
	auto unknown = flags.underlying () & ~known_bits;
	if (unknown)
	{
		if (!first)
		{
			os << ", ";
		}
		os << "unknown(0x" << std::hex << unknown << std::dec << ")";
		first = false;
	}
	if (first)
	{
		os << "<none>";
	}
	return os;
}

template <typename E>
std::string to_string (enum_flags<E> const & flags)
{
	std::ostringstream ss;
	ss << flags;
	return ss.str ();
}
}
