#include <nano/secure/utility.hpp>

#include <nano/lib/interface.h>
#include <nano/node/working.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path nano::working_path ()
{
	auto result (nano::app_path ());
	switch (nano::nano_network)
	{
		case nano::nano_networks::nano_test_network:
			result /= "NanoTest";
			break;
		case nano::nano_networks::nano_beta_network:
			result /= "NanoBeta";
			break;
		case nano::nano_networks::nano_live_network:
			result /= "Nano";
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

std::vector<boost::filesystem::path> nano::remove_temporary_directories ()
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
	return all_unique_paths;
}

void nano::open_or_create (std::fstream & stream_a, std::string const & path_a)
{
	stream_a.open (path_a, std::ios_base::in);
	if (stream_a.fail ())
	{
		stream_a.open (path_a, std::ios_base::out);
	}
	stream_a.close ();
	stream_a.open (path_a, std::ios_base::in | std::ios_base::out);
}
