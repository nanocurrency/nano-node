#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace nano
{
/**
 * Exclusive hold on a data directory, so that a running node and a command writing to its databases never overlap.
 * The hold is an advisory lock on a file inside the directory, which the operating system releases when the holding process ends.
 */
class data_dir_lock final
{
public:
	static constexpr std::string_view filename{ "nano_node.lock" };

	// Takes the directory without waiting, throws std::runtime_error naming the lock file when another process holds it
	explicit data_dir_lock (std::filesystem::path const & data_path);
	~data_dir_lock ();

	data_dir_lock (data_dir_lock const &) = delete;
	data_dir_lock & operator= (data_dir_lock const &) = delete;

	// Lock file through which the data directory is held
	static std::filesystem::path lock_file (std::filesystem::path const & data_path);

private:
	std::filesystem::path const path;
	std::intptr_t handle{ -1 };
};
}
