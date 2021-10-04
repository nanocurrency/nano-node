#include <nano/lib/config.hpp>
#include <nano/secure/utility.hpp>
#include <nano/secure/working.hpp>

#include <boost/filesystem.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path nano::working_path (bool legacy)
{
	static nano::network_constants network_constants;
	auto result (nano::app_path ());
	switch (network_constants.network ())
	{
		case nano::nano_networks::nano_dev_network:
			if (!legacy)
			{
				result /= "BananoDev";
			}
			else
			{
				result /= "BananoDev";
			}
			break;
		case nano::nano_networks::nano_beta_network:
			if (!legacy)
			{
				result /= "BananoBeta";
			}
			else
			{
				result /= "BananoBeta";
			}
			break;
		case nano::nano_networks::nano_live_network:
			if (!legacy)
			{
				result /= "BananoData";
			}
			else
			{
				result /= "Banano";
			}
			break;
		case nano::nano_networks::nano_test_network:
			if (!legacy)
			{
				result /= "BananoTest";
			}
			else
			{
				result /= "BananoTest";
			}
			break;
	}
	return result;
}

boost::filesystem::path nano::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

void nano::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
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
