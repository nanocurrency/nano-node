#include <galileo/secure/utility.hpp>

#include <galileo/lib/interface.h>
#include <galileo/node/working.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path galileo::working_path ()
{
	auto result (galileo::app_path ());
	switch (galileo::galileo_network)
	{
		case galileo::galileo_networks::galileo_test_network:
			result /= "RaiBlocksTest";
			break;
		case galileo::galileo_networks::galileo_beta_network:
			result /= "RaiBlocksBeta";
			break;
		case galileo::galileo_networks::galileo_live_network:
			result /= "RaiBlocks";
			break;
	}
	return result;
}

boost::filesystem::path galileo::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

std::vector<boost::filesystem::path> galileo::remove_temporary_directories ()
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

void galileo::open_or_create (std::fstream & stream_a, std::string const & path_a)
{
	stream_a.open (path_a, std::ios_base::in);
	if (stream_a.fail ())
	{
		stream_a.open (path_a, std::ios_base::out);
	}
	stream_a.close ();
	stream_a.open (path_a, std::ios_base::in | std::ios_base::out);
}
