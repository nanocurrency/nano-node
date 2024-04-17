#include <nano/lib/config.hpp>
#include <nano/secure/utility.hpp>
#include <nano/secure/working.hpp>

#include <boost/system/error_code.hpp>

#include <random>

static std::vector<std::filesystem::path> all_unique_paths;

std::filesystem::path nano::working_path (nano::networks network)
{
	auto result (nano::app_path ());

	if (auto path_override = nano::get_env ("NANO_APP_PATH"))
	{
		result = *path_override;
		std::cerr << "Application path overridden by NANO_APP_PATH environment variable: " << result << std::endl;
	}

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

	auto result = working_path (network) / random_string;

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
