#include <nano/lib/files.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <sys/resource.h>
#endif

std::size_t nano::get_file_descriptor_limit ()
{
	std::size_t fd_limit = std::numeric_limits<std::size_t>::max ();
#ifndef _WIN32
	rlimit limit{};
	if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
	{
		fd_limit = static_cast<std::size_t> (limit.rlim_cur);
	}
#endif
	return fd_limit;
}

void nano::set_file_descriptor_limit (std::size_t limit)
{
#ifndef _WIN32
	rlimit fd_limit{};
	if (-1 == getrlimit (RLIMIT_NOFILE, &fd_limit))
	{
		std::cerr << "WARNING: Unable to get current limits for the number of open file descriptors: " << std::strerror (errno);
		return;
	}

	if (fd_limit.rlim_cur >= limit)
	{
		return;
	}

	fd_limit.rlim_cur = std::min (static_cast<rlim_t> (limit), fd_limit.rlim_max);
	if (-1 == setrlimit (RLIMIT_NOFILE, &fd_limit))
	{
		std::cerr << "WARNING: Unable to set limits for the number of open file descriptors: " << std::strerror (errno);
		return;
	}
#endif
}

void nano::initialize_file_descriptor_limit ()
{
	nano::set_file_descriptor_limit (DEFAULT_FILE_DESCRIPTOR_LIMIT);
	auto limit = nano::get_file_descriptor_limit ();
	if (limit < DEFAULT_FILE_DESCRIPTOR_LIMIT)
	{
		std::cerr << "WARNING: Current file descriptor limit of " << limit << " is lower than the " << DEFAULT_FILE_DESCRIPTOR_LIMIT << " recommended. Node was unable to change it." << std::endl;
	}
}

void nano::remove_all_files_in_dir (std::filesystem::path const & dir)
{
	for (auto & p : std::filesystem::directory_iterator (dir))
	{
		auto path = p.path ();
		if (std::filesystem::is_regular_file (path))
		{
			std::filesystem::remove (path);
		}
	}
}

void nano::move_all_files_to_dir (std::filesystem::path const & from, std::filesystem::path const & to)
{
	for (auto & p : std::filesystem::directory_iterator (from))
	{
		auto path = p.path ();
		if (std::filesystem::is_regular_file (path))
		{
			std::filesystem::rename (path, to / path.filename ());
		}
	}
}