#pragma once

#include <magic_enum.hpp>
#include <magic_enum_containers.hpp>

namespace nano
{
/**
 * Array indexable by enum values
 */
template <typename Index, typename Value>
using enum_array = magic_enum::containers::array<Index, Value>;

/**
 * Same as `magic_enum::enum_values (...)` but ignores reserved values (starting with underscore)
 */
template <class E>
std::vector<E> enum_values ()
{
	std::vector<E> result;
	for (auto const & [val, name] : magic_enum::enum_entries<E> ())
	{
		if (!name.starts_with ('_'))
		{
			result.push_back (val);
		}
	}
	return result;
}

/**
 * Same as `magic_enum::enum_cast (...)` but ignores reserved values (starting with underscore).
 * Case insensitive.
 */
template <class E>
std::optional<E> parse_enum (std::string_view name)
{
	if (name.starts_with ('_'))
	{
		return std::nullopt;
	}
	else
	{
		return magic_enum::enum_cast<E> (name, magic_enum::case_insensitive);
	}
}
}