#pragma once

#include <nano/lib/result.hpp>

#include <filesystem>
#include <fstream>

namespace nano
{
/**
 * Opens \p path for reading.
 * A missing file fails with std::errc::no_such_file_or_directory.
 */
nano::result<std::ifstream> open_file (std::filesystem::path const & path);
}
