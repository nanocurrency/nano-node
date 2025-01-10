#include <celerix/lib/config.hpp>
#include <celerix/lib/env.hpp>
#include <celerix/secure/utility.hpp>
#include <celerix/secure/working.hpp>

#include <boost/system/error_code.hpp>

#include <random>

static std::vector<std::filesystem::path> all_unique_paths;

std::filesystem::path celerix::app_path ()
{
	static auto const path = [] () {
		if (auto value = celerix::env::get ("CELERIX_APP_PATH"))
		{
			std::cerr << "Application path overridden by CELERIX_APP_PATH environment variable: " << *value << std::endl;
			return std::filesystem::path{ *value };
		}
		return celerix::app_path_impl ();
	}();
	return path;
}

std::filesystem::path celerix::working_path (celerix::networks network)
{
	auto result = celerix::app_path ();

	switch (network)
	{
		case celerix::networks::invalid:
			release_assert (false);
			break;
		case celerix::networks::celerix_dev_network:
			result /= "CelerixDev";
			break;
		case celerix::networks::celerix_beta_network:
			result /= "CelerixBeta";
			break;
		case celerix::networks::celerix_live_network:
			result /= "Celerix";
			break;
		case celerix::networks::celerix_test_network:
			result /= "CelerixTest";
			break;
	}
	return result;
}

std::filesystem::path celerix::random_filename ()
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

std::filesystem::path celerix::unique_path (celerix::networks network)
{
	auto result = working_path (network) / random_filename ();

	std::filesystem::create_directories (result);

	all_unique_paths.push_back (result);
	return result;
}

void celerix::remove_temporary_directories ()
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
