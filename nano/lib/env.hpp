#pragma once

#include <optional>
#include <string_view>

/*
 * Get environment variable as a specific type or none if variable is not present.
 */
namespace nano::env
{
std::optional<std::string> get (std::string_view name);

// @throws std::invalid_argument if the value is not a valid boolean
std::optional<bool> get_bool (std::string_view name);
}