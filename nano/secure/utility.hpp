#pragma once

#include <nano/lib/config.hpp>

#include <functional>

namespace nano
{
// OS-specific way of finding a path to a home directory.
std::filesystem::path working_path (nano::networks network = nano::network_constants::active_network);
// Get a unique path within the home directory, used for testing.
// Any directories created at this location will be removed when a test finishes.
std::filesystem::path unique_path (nano::networks network = nano::network_constants::active_network);
// Remove all unique tmp directories created by the process
void remove_temporary_directories ();
}
