#include <nano/lib/config.hpp>
#include <nano/secure/utility.hpp>
#include <nano/secure/working.hpp>

#include <boost/filesystem.hpp>

static std::vector<std::filesystem::path> all_unique_paths;

std::filesystem::path nano::working_path (nano::networks network)
{
	auto result (nano::app_path ());
	switch (network)
	{
		case nano::networks::invalid:
			release_assert (false);
			break;
		case nano::networks::nano_dev_network:
			result /= "NanoDev";
			break;
		case nano::networks::nano_beta_network:
			result /= "NanoBeta";
			break;
		case nano::networks::nano_live_network:
			result /= "Nano";
			break;
		case nano::networks::nano_test_network:
			result /= "NanoTest";
			break;
	}
	return result;
}

std::filesystem::path nano::unique_path (nano::networks network)
{
	auto result (working_path (network) / std::filesystem::unique_path ());
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

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		std::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
}

namespace nano
{
/** A wrapper for handling signals */
std::function<void ()> signal_handler_impl;
void signal_handler (int sig)
{
	if (signal_handler_impl != nullptr)
	{
		signal_handler_impl ();
	}
}
}
