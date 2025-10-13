#include <nano/lib/config.hpp>
#include <nano/lib/env.hpp>
#include <nano/lib/files.hpp>

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <sys/resource.h>
#endif

static std::vector<std::filesystem::path> all_unique_paths;

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

std::filesystem::path nano::app_path ()
{
	static auto const path = [] () {
		if (auto value = nano::env::get ("NANO_APP_PATH"))
		{
			std::cerr << "Application path overridden by NANO_APP_PATH environment variable: " << *value << std::endl;
			return std::filesystem::path{ *value };
		}
		return nano::app_path_impl ();
	}();
	return path;
}

std::filesystem::path nano::working_path (nano::network_type network)
{
	auto result = nano::app_path ();

	switch (network)
	{
		case nano::network_type::invalid:
			release_assert (false);
			break;
		case nano::network_type::nano_dev_network:
			result /= "NanoDev";
			break;
		case nano::network_type::nano_beta_network:
			result /= "NanoBeta";
			break;
		case nano::network_type::nano_live_network:
			result /= "Nano";
			break;
		case nano::network_type::nano_test_network:
			result /= "NanoTest";
			break;
	}
	return result;
}

std::filesystem::path nano::random_filename ()
{
	std::random_device rd;
	std::mt19937 gen (rd ());
	std::uniform_int_distribution<> dis (0, 15);

	const char * hex_chars = "0123456789ABCDEF";
	std::string random_string;
	random_string.reserve (32);

	for (int i = 0; i < 32; ++i)
	{
		random_string += hex_chars[dis (gen)];
	}
	return std::filesystem::path{ random_string };
}

std::filesystem::path nano::unique_path (nano::network_type network)
{
	auto result = working_path (network) / random_filename ();

	std::filesystem::create_directories (result);

	all_unique_paths.push_back (result);
	return result;
}

void nano::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		std::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}
	}
}