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
}

// Needs nested namespace to avoid ADL collisions with magic_enum
namespace nano::enum_util
{
std::string_view name (auto value)
{
	auto name = magic_enum::enum_name (value);
	debug_assert (!name.empty ());
	release_assert (name.size () < 64); // Safety check
	return name;
}

/**
 * Same as `magic_enum::enum_values (...)` but ignores reserved values (starting with underscore) by default.
 */
template <class E>
std::vector<E> const & values (bool ignore_reserved = true)
{
	static std::vector<E> all = [ignore_reserved] () {
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
 * Same as `magic_enum::enum_cast (...)` but ignores reserved values (starting with underscore) by default.
 * Case insensitive.
 */
template <class E>
std::optional<E> try_parse (std::string_view name, bool ignore_reserved = true)
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
 * Same as `magic_enum::enum_cast (...)` but ignores reserved values (starting with underscore) by default.
 * Case insensitive.
 * @throws std::invalid_argument if the name is not found
 */
template <class E>
E parse (std::string_view name, bool ignore_reserved = true)
{
	auto value = try_parse<E> (name, ignore_reserved);
	if (value)
	{
		return *value;
	}
	throw std::invalid_argument ("Invalid value of " + std::string{ magic_enum::enum_type_name<E> () } + ": \"" + std::string{ name } + "\"");
}

template <typename T, typename S>
consteval void ensure_all_castable ()
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

template <class T, class S>
T cast (S value)
{
	ensure_all_castable<T, S> ();

	auto conv = magic_enum::enum_cast<T> (nano::enum_util::name (value));
	debug_assert (conv);
	return conv.value_or (T{});
}
}