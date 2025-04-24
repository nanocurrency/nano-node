#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/env.hpp>
#include <nano/lib/logging.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <valgrind/valgrind.h>

namespace nano
{
uint8_t get_major_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (NANO_MAJOR_VERSION_STRING));
}
uint8_t get_minor_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (NANO_MINOR_VERSION_STRING));
}
uint8_t get_patch_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (NANO_PATCH_VERSION_STRING));
}
uint8_t get_pre_release_node_version ()
{
	return boost::numeric_cast<uint8_t> (boost::lexical_cast<int> (NANO_PRE_RELEASE_VERSION_STRING));
}

void force_nano_dev_network ()
{
	nano::network_constants::set_active_network (nano::networks::nano_dev_network);
}

bool is_dev_run ()
{
	return nano::network_constants::get_active_network () == nano::networks::nano_dev_network;
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

bool memory_intensive_instrumentation ()
{
	auto env = nano::env::get<bool> ("NANO_MEMORY_INTENSIVE");
	if (env)
	{
		return env.value ();
	}
	return is_tsan_build () || nano::running_within_valgrind ();
}

bool slow_instrumentation ()
{
	return is_tsan_build () || nano::running_within_valgrind ();
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

uint16_t nano::test_node_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = nano::env::get<uint16_t> ("NANO_TEST_NODE_PORT"))
		{
			std::cerr << "Node port overridden by NANO_TEST_NODE_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17075);
}

uint16_t nano::test_rpc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = nano::env::get<uint16_t> ("NANO_TEST_RPC_PORT"))
		{
			std::cerr << "RPC port overridden by NANO_TEST_RPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17076);
}

uint16_t nano::test_ipc_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = nano::env::get<uint16_t> ("NANO_TEST_IPC_PORT"))
		{
			std::cerr << "IPC port overridden by NANO_TEST_IPC_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17077);
}

uint16_t nano::test_websocket_port ()
{
	static auto const test_env = [] () -> std::optional<uint16_t> {
		if (auto value = nano::env::get<uint16_t> ("NANO_TEST_WEBSOCKET_PORT"))
		{
			std::cerr << "Websocket port overridden by NANO_TEST_WEBSOCKET_PORT environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (17078);
}

uint32_t nano::test_scan_wallet_reps_delay ()
{
	static auto const test_env = [] () -> std::optional<uint32_t> {
		if (auto value = nano::env::get<uint32_t> ("NANO_TEST_WALLET_SCAN_REPS_DELAY"))
		{
			std::cerr << "Wallet scan interval overridden by NANO_TEST_WALLET_SCAN_REPS_DELAY environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return test_env.value_or (900000); // 15 minutes default
}

std::array<uint8_t, 2> nano::test_magic_number ()
{
	static auto const test_env = [] () -> std::optional<std::string> {
		if (auto value = nano::env::get<std::string> ("NANO_TEST_MAGIC_NUMBER"))
		{
			std::cerr << "Magic number overridden by NANO_TEST_MAGIC_NUMBER environment variable: " << *value << std::endl;
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

size_t nano::queue_warning_threshold ()
{
	static auto const env_override = [] () -> std::optional<size_t> {
		if (auto value = nano::env::get<size_t> ("NANO_QUEUE_WARNING_THRESHOLD"))
		{
			std::cerr << "Queue warning threshold overridden by NANO_QUEUE_WARNING_THRESHOLD environment variable: " << *value << std::endl;
			return *value;
		}
		return std::nullopt;
	}();
	return env_override.value_or (100);
}

std::string_view nano::to_string (nano::networks network)
{
	switch (network)
	{
		case nano::networks::invalid:
			return "invalid";
		case nano::networks::nano_beta_network:
			return "beta";
		case nano::networks::nano_dev_network:
			return "dev";
		case nano::networks::nano_live_network:
			return "live";
		case nano::networks::nano_test_network:
			return "test";
			// default case intentionally omitted to cause warnings for unhandled enums
	}

	return "n/a";
}

// Using std::cerr here, since logging may not be initialized yet
nano::tomlconfig nano::load_toml_file (const std::filesystem::path & config_filename, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
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
		nano::tomlconfig toml;
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
		nano::tomlconfig toml;
		auto error = toml.read (config_overrides_stream);
		if (error)
		{
			throw std::runtime_error (error.get_message ());
		}
		std::cerr << "Config file `" << config_filename.string () << "` not found, using default configuration" << std::endl;
		return toml;
	}
}
