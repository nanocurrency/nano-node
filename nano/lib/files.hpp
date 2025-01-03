#pragma once

#include <filesystem>

namespace nano
{
/*
 * Functions for managing filesystem permissions, platform specific
 */
void set_umask ();
void set_secure_perm_directory (std::filesystem::path const & path);
void set_secure_perm_directory (std::filesystem::path const & path, std::error_code & ec);
void set_secure_perm_file (std::filesystem::path const & path);
void set_secure_perm_file (std::filesystem::path const & path, std::error_code & ec);

/*
 * Function to check if running Windows as an administrator
 */
bool is_windows_elevated ();

/*
 * Function to check if the Windows Event log registry key exists
 */
bool event_log_reg_entry_exists ();

/*
 * Create the load memory addresses for the executable and shared libraries.
 */
void create_load_memory_address_files ();

/**
 * Some systems, especially in virtualized environments, may have very low file descriptor limits,
 * causing the node to fail. This function attempts to query the limit and returns the value. If the
 * limit cannot be queried, or running on a Windows system, this returns max-value of std::size_t.
 * Increasing the limit programmatically can be done only for the soft limit, the hard one requiring
 * super user permissions to modify.
 */
std::size_t get_file_descriptor_limit ();
void set_file_descriptor_limit (std::size_t limit);
/**
 * This should be called from entry points. It sets the file descriptor limit to the maximum allowed and logs any errors.
 */
constexpr std::size_t DEFAULT_FILE_DESCRIPTOR_LIMIT = 16384;
void initialize_file_descriptor_limit ();

void remove_all_files_in_dir (std::filesystem::path const & dir);
void move_all_files_to_dir (std::filesystem::path const & from, std::filesystem::path const & to);
}