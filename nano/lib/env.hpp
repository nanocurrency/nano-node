#pragma once

#include <optional>
#include <string_view>

namespace nano
{
/*
 * Get environment variable as a specific type or none if variable is not present.
 */
std::optional<std::string> get_env (std::string_view name);

// @throws std::invalid_argument if the value is not a valid boolean
std::optional<bool> get_env_bool (std::string_view name);
}