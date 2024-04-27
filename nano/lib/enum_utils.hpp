#pragma once

#include <nano/lib/utility.hpp>

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
std::vector<E> enum_values (bool ignore_reserved = true)
{
	std::vector<E> result;
	for (auto const & [val, name] : magic_enum::enum_entries<E> ())
	{
		if (!ignore_reserved || !name.starts_with ('_'))
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
std::optional<E> enum_parse (std::string_view name, bool ignore_reserved = true)
{
	if (ignore_reserved && name.starts_with ('_'))
	{
		return std::nullopt;
	}
	else
	{
		return magic_enum::enum_cast<E> (name, magic_enum::case_insensitive);
	}
}

template <class T, class S>
T enum_cast (S value)
{
	auto conv = magic_enum::enum_cast<T> (magic_enum::enum_name (value));
	debug_assert (conv);
	return conv.value_or (T{});
}
}