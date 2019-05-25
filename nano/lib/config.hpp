#pragma once

#include <nano/lib/errors.hpp>
#include <nano/lib/numbers.hpp>

#include <boost/filesystem.hpp>

#include <array>
#include <chrono>
#include <string>

#define xstr(a) ver_str (a)
#define ver_str(a) #a

/**
* Returns build version information
*/
static const char * NANO_MAJOR_MINOR_VERSION = xstr (NANO_VERSION_MAJOR) "." xstr (NANO_VERSION_MINOR);
static const char * NANO_MAJOR_MINOR_RC_VERSION = xstr (NANO_VERSION_MAJOR) "." xstr (NANO_VERSION_MINOR) "RC" xstr (NANO_VERSION_PATCH);
/** Is TSAN/ASAN test build */
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
static const bool is_sanitizer_build = true;
#else
static const bool is_sanitizer_build = false;
#endif
#else
static const bool is_sanitizer_build = false;
#endif

namespace nano
{
/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used in integral comparisons; do not change.
 */
enum class nano_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	nano_test_network = 0,
	rai_test_network = 0,
	// Normal work parameters, secret beta genesis key, beta IP ports
	nano_beta_network = 1,
	rai_beta_network = 1,
	// Normal work parameters, secret live key, live IP ports
	nano_live_network = 2,
	rai_live_network = 2,
};

class network_constants
{
public:
	network_constants () :
	network_constants (network_constants::active_network)
	{
	}

	network_constants (nano_networks network_a) :
	current_network (network_a)
	{
		// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
		uint64_t constexpr publish_test_threshold = 0xff00000000000000;
		uint64_t constexpr publish_full_threshold = 0xffffffc000000000;
		publish_threshold = is_test_network () ? publish_test_threshold : publish_full_threshold;

		default_node_port = is_live_network () ? 7075 : is_beta_network () ? 54000 : 44000;
		default_rpc_port = is_live_network () ? 7076 : is_beta_network () ? 55000 : 45000;
		default_ipc_port = is_live_network () ? 7077 : is_beta_network () ? 56000 : 46000;
		default_websocket_port = is_live_network () ? 7078 : is_beta_network () ? 57000 : 47000;
		request_interval_ms = is_test_network () ? (is_sanitizer_build ? 100 : 20) : 16000;
	}

	/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
	nano_networks current_network;
	uint64_t publish_threshold;
	uint16_t default_node_port;
	uint16_t default_rpc_port;
	uint16_t default_ipc_port;
	uint16_t default_websocket_port;
	unsigned request_interval_ms;

	/** Returns the network this object contains values for */
	nano_networks network () const
	{
		return current_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network
	 */
	static void set_active_network (nano_networks network_a)
	{
		active_network = network_a;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "test"
	 */
	static nano::error set_active_network (std::string network_a)
	{
		nano::error err;
		if (network_a == "live")
		{
			active_network = nano::nano_networks::nano_live_network;
		}
		else if (network_a == "beta")
		{
			active_network = nano::nano_networks::nano_beta_network;
		}
		else if (network_a == "test")
		{
			active_network = nano::nano_networks::nano_test_network;
		}
		else
		{
			err = "Invalid network. Valid values are live, beta and test.";
		}
		return err;
	}

	const char * get_current_network_as_string () const
	{
		return is_live_network () ? "live" : is_beta_network () ? "beta" : "test";
	}

	bool is_live_network () const
	{
		return current_network == nano_networks::nano_live_network;
	}
	bool is_beta_network () const
	{
		return current_network == nano_networks::nano_beta_network;
	}
	bool is_test_network () const
	{
		return current_network == nano_networks::nano_test_network;
	}

	/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
	static nano::nano_networks active_network;
};

inline boost::filesystem::path get_config_path (boost::filesystem::path const & data_path)
{
	return data_path / "config.json";
}

inline boost::filesystem::path get_rpc_config_path (boost::filesystem::path const & data_path)
{
	return data_path / "rpc_config.json";
}

/** Called by gtest_main to enforce test network */
void force_nano_test_network ();
}
