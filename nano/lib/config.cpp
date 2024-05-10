#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/env.hpp>
#include <nano/lib/logging.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <valgrind/valgrind.h>

namespace
{
// useful for boost_lexical cast to allow conversion of hex strings
template <typename ElemT>
struct HexTo
{
	ElemT value;
	operator ElemT () const
	{
		return value;
	}
	friend std::istream & operator>> (std::istream & in, HexTo & out)
	{
		in >> std::hex >> out.value;
		return in;
	}
};
} // namespace

nano::work_thresholds const nano::work_thresholds::publish_full (
0xffffffc000000000,
0xfffffff800000000, // 8x higher than epoch_1
0xfffffe0000000000 // 8x lower than epoch_1
);

nano::work_thresholds const nano::work_thresholds::publish_beta (
0xfffff00000000000, // 64x lower than publish_full.epoch_1
0xfffff00000000000, // same as epoch_1
0xffffe00000000000 // 2x lower than epoch_1
);

nano::work_thresholds const nano::work_thresholds::publish_dev (
0xfe00000000000000, // Very low for tests
0xffc0000000000000, // 8x higher than epoch_1
0xf000000000000000 // 8x lower than epoch_1
);

nano::work_thresholds const nano::work_thresholds::publish_test ( // defaults to live network levels
get_env_threshold_or_default ("NANO_TEST_EPOCH_1", 0xffffffc000000000),
get_env_threshold_or_default ("NANO_TEST_EPOCH_2", 0xfffffff800000000), // 8x higher than epoch_1
get_env_threshold_or_default ("NANO_TEST_EPOCH_2_RECV", 0xfffffe0000000000) // 8x lower than epoch_1
);

uint64_t nano::work_thresholds::threshold_entry (nano::work_version const version_a, nano::block_type const type_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	if (type_a == nano::block_type::state)
	{
		switch (version_a)
		{
			case nano::work_version::work_1:
				result = entry;
				break;
			default:
				debug_assert (false && "Invalid version specified to work_threshold_entry");
		}
	}
	else
	{
		result = epoch_1;
	}
	return result;
}

#ifndef NANO_FUZZER_TEST
uint64_t nano::work_thresholds::value (nano::root const & root_a, uint64_t work_a) const
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work_a), sizeof (work_a));
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}
#else
uint64_t nano::work_thresholds::value (nano::root const & root_a, uint64_t work_a) const
{
	return base + 1;
}
#endif

uint64_t nano::work_thresholds::threshold (nano::block_details const & details_a) const
{
	static_assert (nano::epoch::max == nano::epoch::epoch_2, "work_v1::threshold is ill-defined");

	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (details_a.epoch)
	{
		case nano::epoch::epoch_2:
			result = (details_a.is_receive || details_a.is_epoch) ? epoch_2_receive : epoch_2;
			break;
		case nano::epoch::epoch_1:
		case nano::epoch::epoch_0:
			result = epoch_1;
			break;
		default:
			debug_assert (false && "Invalid epoch specified to work_v1 ledger work_threshold");
	}
	return result;
}

uint64_t nano::work_thresholds::threshold (nano::work_version const version_a, nano::block_details const details_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case nano::work_version::work_1:
			result = threshold (details_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to ledger work_threshold");
	}
	return result;
}

double nano::work_thresholds::normalized_multiplier (double const multiplier_a, uint64_t const threshold_a) const
{
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	/* Normalization rules
	ratio = multiplier of max work threshold (send epoch 2) from given threshold
	i.e. max = 0xfe00000000000000, given = 0xf000000000000000, ratio = 8.0
	normalized = (multiplier + (ratio - 1)) / ratio;
	Epoch 1
	multiplier	 | normalized
	1.0 		 | 1.0
	9.0 		 | 2.0
	25.0 		 | 4.0
	Epoch 2 (receive / epoch subtypes)
	multiplier	 | normalized
	1.0 		 | 1.0
	65.0 		 | 2.0
	241.0 		 | 4.0
	*/
	if (threshold_a == epoch_1 || threshold_a == epoch_2_receive)
	{
		auto ratio (nano::difficulty::to_multiplier (epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = (multiplier + (ratio - 1.0)) / ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

double nano::work_thresholds::denormalized_multiplier (double const multiplier_a, uint64_t const threshold_a) const
{
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	if (threshold_a == epoch_1 || threshold_a == epoch_2_receive)
	{
		auto ratio (nano::difficulty::to_multiplier (epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = multiplier * ratio + 1.0 - ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

uint64_t nano::work_thresholds::threshold_base (nano::work_version const version_a) const
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case nano::work_version::work_1:
			result = base;
			break;
		default:
			debug_assert (false && "Invalid version specified to work_threshold_base");
	}
	return result;
}

uint64_t nano::work_thresholds::difficulty (nano::work_version const version_a, nano::root const & root_a, uint64_t const work_a) const
{
	uint64_t result{ 0 };
	switch (version_a)
	{
		case nano::work_version::work_1:
			result = value (root_a, work_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to work_difficulty");
	}
	return result;
}

uint64_t nano::work_thresholds::difficulty (nano::block const & block_a) const
{
	return difficulty (block_a.work_version (), block_a.root (), block_a.block_work ());
}

bool nano::work_thresholds::validate_entry (nano::work_version const version_a, nano::root const & root_a, uint64_t const work_a) const
{
	return difficulty (version_a, root_a, work_a) < threshold_entry (version_a, nano::block_type::state);
}

bool nano::work_thresholds::validate_entry (nano::block const & block_a) const
{
	return difficulty (block_a) < threshold_entry (block_a.work_version (), block_a.type ());
}

namespace nano
{
char const * network_constants::active_network_err_msg = "Invalid network. Valid values are live, test, beta and dev.";

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

uint64_t get_env_threshold_or_default (char const * variable_name, uint64_t const default_value)
{
	auto * value = getenv (variable_name);
	return value ? boost::lexical_cast<HexTo<uint64_t>> (value) : default_value;
}

void force_nano_dev_network ()
{
	nano::network_constants::set_active_network (nano::networks::nano_dev_network);
}

bool running_within_valgrind ()
{
	return (RUNNING_ON_VALGRIND > 0);
}

bool memory_intensive_instrumentation ()
{
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
