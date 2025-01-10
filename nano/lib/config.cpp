#include <celerix/crypto/blake2/blake2.h>
#include <celerix/lib/block_type.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/config.hpp>
#include <celerix/lib/constants.hpp>
#include <celerix/lib/env.hpp>
#include <celerix/lib/logging.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <valgrind/valgrind.h>

namespace celerix
{
uint8_t get_major_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (CELERIX_MAJOR_VERSION_STRING));
}
uint8_t get_minor_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (CELERIX_MINOR_VERSION_STRING));
}
uint8_t get_patch_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (CELERIX_PATCH_VERSION_STRING));
}
uint8_t get_pre_release_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (CELERIX_PRE_RELEASE_VERSION_STRING));
}

void force_celerix_dev_network ()
{
	celerix::network_constants::set_active_network (celerix::networks::celerix_dev_network);
}

bool is_dev_run ()
{
	return celerix::network_constants::get_active_network () == celerix::networks::celerix_dev_network;
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

bool memory_intensive_instrumentation ()
{
	auto env = celerix::env::get<bool> ("CELERIX_MEMORY_INTENSIVE");
	if (env)
	{
		return env.value ();
	}
	return is_tsan_build () || celerix::running_within_valgrind ();
}

bool slow_instrumentation ()
{
	return is_tsan_build () || celerix::running_within_valgrind ();
}

std::string get_node_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / "config-node.toml").string ();
}

std::string get_rpc_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / "config-rpc.toml").string ();
}

std::string get_qtwallet_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / "config-qtwallet.toml").string ();
}

std::string get_access_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / "config-access.toml").string ();
}

std::string get_tls_toml_config_path (std::filesystem::path const & data_path)
{
	return (data_path / "config-tls.toml").string ();
}
}

uint16_t celerix::test_node_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = celerix::env::get<uint16_t> ("CELERIX_TEST_NODE_PORT"))
		{
			std::cerr << "Node port overridden by CELERIX_TEST_NODE_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17075);
}

uint16_t celerix::test_rpc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = celerix::env::get<uint16_t> ("CELERIX_TEST_RPC_PORT"))
		{
			std::cerr << "RPC port overridden by CELERIX_TEST_RPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17076);
}

uint16_t celerix::test_ipc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = celerix::env::get<uint16_t> ("CELERIX_TEST_IPC_PORT"))
		{
			std::cerr << "IPC port overridden by CELERIX_TEST_IPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17077);
}

uint16_t celerix::test_websocket_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = celerix::env::get<uint16_t> ("CELERIX_TEST_WEBSOCKET_PORT"))
		{
			std::cerr << "Websocket port overridden by CELERIX_TEST_WEBSOCKET_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17078);
}

uint32_t celerix::test_scan_wallet_reps_delay ()
{
	static auto const test_env = [] () -> std::optional<uint32_t> {
		if (auto value = celerix::env::get<uint32_t> ("CELERIX_TEST_WALLET_SCAN_REPS_DELAY"))
		{
			std::cerr << "Wallet scan interval overridden by CELERIX_TEST_WALLET_SCAN_REPS_DELAY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (900000); // 15 minutes default
}

std::array<uint8_t, 2> celerix::test_magic_number ()
{
	static auto const test_env = [] () -> std::optional<std::string> {
		if (auto value = celerix::env::get<std::string> ("CELERIX_TEST_MAGIC_NUMBER"))
		{
			std::cerr << "Magic number overridden by CELERIX_TEST_MAGIC_NUMBER environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();

	auto value = test_env.value_or ("RX");
	release_assert (value.size () == 2);
	std::array<uint8_t, 2> ret{};
	std::copy (value.begin (), value.end (), ret.data ());
	return ret;
}

std::string_view celerix::to_string (celerix::networks network)
{
	switch (network)
	{
		case celerix::networks::invalid:
			return "invalid";
		case celerix::networks::celerix_beta_network:
			return "beta";
		case celerix::networks::celerix_dev_network:
			return "dev";
		case celerix::networks::celerix_live_network:
			return "live";
		case celerix::networks::celerix_test_network:
			return "test";
			// default case intentionally omitted to cause warnings for unhandled enums
	}

	return "n/a";
}

// Using std::cerr here, since logging may not be initialized yet
celerix::tomlconfig celerix::load_toml_file (const std::filesystem::path & config_filename, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
{
	std::stringstream config_overrides_stream;
	for (auto const & entry : config_overrides)
	{
		config_overrides_stream << entry << std::endl;
	}
	config_overrides_stream << std::endl;

	// Make sure we don't create an empty toml file if it doesn't exist. Running without a toml file is the default.
	auto toml_config_path = data_path / config_filename;
	if (std::filesystem::exists (toml_config_path))
	{
		celerix::tomlconfig toml;
		auto error = toml.read (config_overrides_stream, toml_config_path);
		if (error)
		{
			throw std::runtime_error (error.get_message ());
		}
		std::cerr << "Config file `" << config_filename.string () << "` loaded from node data directory: " << toml_config_path.string () << std::endl;
		return toml;
	}
	else
	{
		// If no config was found, return an empty config with overrides applied
		celerix::tomlconfig toml;
		auto error = toml.read (config_overrides_stream);
		if (error)
		{
			throw std::runtime_error (error.get_message ());
		}
		std::cerr << "Config file `" << config_filename.string () << "` not found, using default configuration" << std::endl;
		return toml;
	}
}
