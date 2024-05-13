#pragma once

#include <boost/lexical_cast.hpp>

#include <optional>
#include <string_view>

namespace nano::env
{
/**
 * Get environment variable as a specific type or none if variable is not present.
 */
std::optional<std::string> get (std::string_view name);

/**
 * Get environment variable as a specific type or none if variable is not present.
 * @throws std::invalid_argument if the value cannot be converted
 */
template <typename T>
std::optional<T> get (std::string_view name)
{
	if (auto value = get (name))
	{
		try
		{
			return boost::lexical_cast<T> (*value);
		}
		catch (boost::bad_lexical_cast const &)
		{
			throw std::invalid_argument ("Invalid environment value: " + *value);
		}
	}
	return std::nullopt;
}

/**
 * Specialization for boolean values.
 * @throws std::invalid_argument if the value is not a valid boolean
 */
template <>
std::optional<bool> get (std::string_view name);
}