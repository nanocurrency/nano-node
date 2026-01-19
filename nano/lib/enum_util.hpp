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

template <typename T, typename S>
consteval void ensure_all_enum_castable ()
{
	for (auto value : magic_enum::enum_values<S> ())
	{
		if (!magic_enum::enum_cast<T> (magic_enum::enum_name (value)))
		{
			// If this fails, it means that the target enum is missing a value present in the source enum
			throw std::logic_error ("Value of " + std::string{ magic_enum::enum_type_name<S> () } + " (" + std::string{ magic_enum::enum_name (value) } + ") cannot be cast to " + std::string{ magic_enum::enum_type_name<T> () });
		}
	}
}

/**
 * Returns the string name of an enum value.
 * Wraps magic_enum::enum_name with safety checks.
 */
std::string_view enum_to_string (auto value)
{
	auto name = magic_enum::enum_name (value);
	debug_assert (!name.empty ());
	release_assert (name.size () < 64); // Safety check
	return name;
}

/**
 * Returns all valid enum values, ignoring reserved values (starting with underscore) by default.
 * Same as magic_enum::enum_values but filters reserved values.
 */
template <class E, bool ignore_reserved = true>
std::vector<E> const & enum_values ()
{
	static std::vector<E> all = [] () {
		std::vector<E> result;
		for (auto const & [val, name] : magic_enum::enum_entries<E> ())
		{
			if (!ignore_reserved || !name.starts_with ('_'))
			{
				result.push_back (val);
			}
		}
		return result;
	}();
	return all;
}

/**
 * Parses a string to an enum value. Case insensitive.
 * Ignores reserved values (starting with underscore) by default.
 * Returns std::nullopt if the name is not found.
 */
template <class E>
std::optional<E> enum_try_parse (std::string_view name, bool ignore_reserved = true)
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

/**
 * Parses a string to an enum value. Case insensitive.
 * Ignores reserved values (starting with underscore) by default.
 * @throws std::invalid_argument if the name is not found
 */
template <class E>
E enum_parse (std::string_view name, bool ignore_reserved = true)
{
	auto value = enum_try_parse<E> (name, ignore_reserved);
	if (value)
	{
		return *value;
	}
	throw std::invalid_argument ("Invalid value of " + std::string{ magic_enum::enum_type_name<E> () } + ": \"" + std::string{ name } + "\"");
}

/**
 * Converts an enum value to another enum type by matching names.
 * Validates at compile-time that all source enum values exist in target enum.
 */
template <class T, class S>
T enum_convert (S value)
{
	ensure_all_enum_castable<T, S> ();

	auto conv = magic_enum::enum_cast<T> (nano::enum_to_string (value));
	debug_assert (conv);
	return conv.value_or (T{});
}
}