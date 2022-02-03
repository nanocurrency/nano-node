#pragma once

#include <boost/config.hpp>
#include <boost/version.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

namespace boost
{
namespace filesystem
{
	class path;
}
}

#define xstr(a) ver_str (a)
#define ver_str(a) #a

/**
* Returns build version information
*/
char const * const NANO_VERSION_STRING = xstr (TAG_VERSION_STRING);
char const * const NANO_MAJOR_VERSION_STRING = xstr (MAJOR_VERSION_STRING);
char const * const NANO_MINOR_VERSION_STRING = xstr (MINOR_VERSION_STRING);
char const * const NANO_PATCH_VERSION_STRING = xstr (PATCH_VERSION_STRING);
char const * const NANO_PRE_RELEASE_VERSION_STRING = xstr (PRE_RELEASE_VERSION_STRING);

char const * const BUILD_INFO = xstr (GIT_COMMIT_HASH BOOST_COMPILER) " \"BOOST " xstr (BOOST_VERSION) "\" BUILT " xstr (__DATE__);

/** Is TSAN/ASAN dev build */
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
bool const is_sanitizer_build = true;
#else
bool const is_sanitizer_build = false;
#endif
// GCC builds
#elif defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
const bool is_sanitizer_build = true;
#else
bool const is_sanitizer_build = false;
#endif

namespace nano
{
uint8_t get_major_node_version ();
uint8_t get_minor_node_version ();
uint8_t get_patch_node_version ();
uint8_t get_pre_release_node_version ();

std::string get_env_or_default (char const * variable_name, std::string const default_value);
uint64_t get_env_threshold_or_default (char const * variable_name, uint64_t const default_value);

uint16_t test_node_port ();
uint16_t test_rpc_port ();
uint16_t test_ipc_port ();
uint16_t test_websocket_port ();
std::array<uint8_t, 2> test_magic_number ();

/**
 * Network variants with different genesis blocks and network parameters
 */
enum class networks : uint16_t
{
	invalid = 0x0,
	// Low work parameters, publicly known genesis key, dev IP ports
	nano_dev_network = 0x5241, // 'R', 'A'
	// Normal work parameters, secret beta genesis key, beta IP ports
	nano_beta_network = 0x5242, // 'R', 'B'
	// Normal work parameters, secret live key, live IP ports
	nano_live_network = 0x5243, // 'R', 'C'
	// Normal work parameters, secret test genesis key, test IP ports
	nano_test_network = 0x5258, // 'R', 'X'
};

enum class work_version
{
	unspecified,
	work_1
};
enum class block_type : uint8_t;
class root;
class block;
class block_details;

class work_thresholds
{
public:
	uint64_t const epoch_1;
	uint64_t const epoch_2;
	uint64_t const epoch_2_receive;

	// Automatically calculated. The base threshold is the maximum of all thresholds and is used for all work multiplier calculations
	uint64_t const base;

	// Automatically calculated. The entry threshold is the minimum of all thresholds and defines the required work to enter the node, but does not guarantee a block is processed
	uint64_t const entry;

	constexpr work_thresholds (uint64_t epoch_1_a, uint64_t epoch_2_a, uint64_t epoch_2_receive_a) :
		epoch_1 (epoch_1_a), epoch_2 (epoch_2_a), epoch_2_receive (epoch_2_receive_a),
		base (std::max ({ epoch_1, epoch_2, epoch_2_receive })),
		entry (std::min ({ epoch_1, epoch_2, epoch_2_receive }))
	{
	}
	work_thresholds () = delete;
	work_thresholds operator= (nano::work_thresholds const & other_a)
	{
		return other_a;
	}

	uint64_t threshold_entry (nano::work_version const, nano::block_type const) const;
	uint64_t threshold (nano::block_details const &) const;
	// Ledger threshold
	uint64_t threshold (nano::work_version const, nano::block_details const) const;
	uint64_t threshold_base (nano::work_version const) const;
	uint64_t value (nano::root const & root_a, uint64_t work_a) const;
	double normalized_multiplier (double const, uint64_t const) const;
	double denormalized_multiplier (double const, uint64_t const) const;
	uint64_t difficulty (nano::work_version const, nano::root const &, uint64_t const) const;
	uint64_t difficulty (nano::block const & block_a) const;
	bool validate_entry (nano::work_version const, nano::root const &, uint64_t const) const;
	bool validate_entry (nano::block const &) const;

	/** Network work thresholds. Define these inline as constexpr when moving to cpp17. */
	static nano::work_thresholds const publish_full;
	static nano::work_thresholds const publish_beta;
	static nano::work_thresholds const publish_dev;
	static nano::work_thresholds const publish_test;
};

class network_constants
{
public:
	network_constants (nano::work_thresholds & work, nano::networks network_a) :
		current_network (network_a),
		work{ work }
	{
		// A representative is classified as principal based on its weight and this factor
		principal_weight_factor = 1000; // 0.1%

		default_node_port = is_live_network () ? 7075 : is_beta_network () ? 54000
		: is_test_network ()                                               ? test_node_port ()
																		   : 44000;
		default_rpc_port = is_live_network () ? 7076 : is_beta_network () ? 55000
		: is_test_network ()                                              ? test_rpc_port ()
																		  : 45000;
		default_ipc_port = is_live_network () ? 7077 : is_beta_network () ? 56000
		: is_test_network ()                                              ? test_ipc_port ()
																		  : 46000;
		default_websocket_port = is_live_network () ? 7078 : is_beta_network () ? 57000
		: is_test_network ()                                                    ? test_websocket_port ()
																				: 47000;
		request_interval_ms = is_dev_network () ? 20 : 500;
		cleanup_period = is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (60);
		idle_timeout = is_dev_network () ? cleanup_period * 15 : cleanup_period * 2;
		silent_connection_tolerance_time = std::chrono::seconds (120);
		syn_cookie_cutoff = std::chrono::seconds (5);
		bootstrap_interval = std::chrono::seconds (15 * 60);
		max_peers_per_ip = is_dev_network () ? 20 : 10;
		max_peers_per_subnetwork = max_peers_per_ip * 4;
		ipv6_subnetwork_prefix_for_limiting = 64; // Equivalent to network prefix /64.
		peer_dump_interval = is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	}

	/** Error message when an invalid network is specified */
	static char const * active_network_err_msg;

	/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
	nano::networks current_network{ nano::network_constants::active_network };
	nano::work_thresholds & work;

	unsigned principal_weight_factor;
	uint16_t default_node_port;
	uint16_t default_rpc_port;
	uint16_t default_ipc_port;
	uint16_t default_websocket_port;
	unsigned request_interval_ms;

	std::chrono::seconds cleanup_period;
	std::chrono::milliseconds cleanup_period_half () const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds> (cleanup_period) / 2;
	}
	std::chrono::seconds cleanup_cutoff () const
	{
		return cleanup_period * 5;
	}
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds idle_timeout;
	std::chrono::seconds silent_connection_tolerance_time;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::seconds bootstrap_interval;
	/** Maximum number of peers per IP. It is also the max number of connections per IP */
	size_t max_peers_per_ip;
	/** Maximum number of peers per subnetwork */
	size_t max_peers_per_subnetwork;
	size_t ipv6_subnetwork_prefix_for_limiting;
	std::chrono::seconds peer_dump_interval;

	/** Returns the network this object contains values for */
	nano::networks network () const
	{
		return current_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network
	 */
	static void set_active_network (nano::networks network_a)
	{
		active_network = network_a;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "dev"
	 */
	static bool set_active_network (std::string network_a)
	{
		auto error{ false };
		if (network_a == "live")
		{
			active_network = nano::networks::nano_live_network;
		}
		else if (network_a == "beta")
		{
			active_network = nano::networks::nano_beta_network;
		}
		else if (network_a == "dev")
		{
			active_network = nano::networks::nano_dev_network;
		}
		else if (network_a == "test")
		{
			active_network = nano::networks::nano_test_network;
		}
		else
		{
			error = true;
		}
		return error;
	}

	char const * get_current_network_as_string ()
	{
		return is_live_network () ? "live" : is_beta_network () ? "beta"
		: is_test_network ()                                    ? "test"
																: "dev";
	}

	bool is_live_network () const
	{
		return current_network == nano::networks::nano_live_network;
	}
	bool is_beta_network () const
	{
		return current_network == nano::networks::nano_beta_network;
	}
	bool is_dev_network () const
	{
		return current_network == nano::networks::nano_dev_network;
	}
	bool is_test_network () const
	{
		return current_network == nano::networks::nano_test_network;
	}

	/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
	static nano::networks active_network;
	/** Current protocol version */
	uint8_t const protocol_version = 0x12;
	/** Minimum accepted protocol version */
	uint8_t const protocol_version_min = 0x12;
};

std::string get_node_toml_config_path (boost::filesystem::path const & data_path);
std::string get_rpc_toml_config_path (boost::filesystem::path const & data_path);
std::string get_access_toml_config_path (boost::filesystem::path const & data_path);
std::string get_qtwallet_toml_config_path (boost::filesystem::path const & data_path);
std::string get_tls_toml_config_path (boost::filesystem::path const & data_path);

/** Checks if we are running inside a valgrind instance */
bool running_within_valgrind ();

/** Set the active network to the dev network */
void force_nano_dev_network ();
}
