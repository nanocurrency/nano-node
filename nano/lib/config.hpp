#pragma once

#include <nano/lib/tomlconfig.hpp>

#include <boost/config.hpp>
#include <boost/version.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

using namespace std::chrono_literals;

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

/*
 * Sanitizer info
 */
namespace nano
{
consteval bool is_asan_build ()
{
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
	return true;
#else
	return false;
#endif
	// GCC builds
#elif defined(__SANITIZE_ADDRESS__)
	return true;
#else
	return false;
#endif
}

consteval bool is_tsan_build ()
{
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
	return true;
#else
	return false;
#endif
	// GCC builds
#elif defined(__SANITIZE_THREAD__)
	return true;
#else
	return false;
#endif
}

/** Checks if we are running with either AddressSanitizer or ThreadSanitizer */
consteval bool is_sanitizer_build ()
{
	return is_asan_build () || is_tsan_build ();
}
}

namespace nano
{
uint8_t get_major_node_version ();
uint8_t get_minor_node_version ();
uint8_t get_patch_node_version ();
uint8_t get_pre_release_node_version ();

uint16_t test_node_port ();
uint16_t test_rpc_port ();
uint16_t test_ipc_port ();
uint16_t test_websocket_port ();
std::array<uint8_t, 2> test_magic_number ();
uint32_t test_scan_wallet_reps_delay (); // How often to scan for representatives in local wallet, in milliseconds

std::string get_node_toml_config_path (std::filesystem::path const & data_path);
std::string get_rpc_toml_config_path (std::filesystem::path const & data_path);
std::string get_access_toml_config_path (std::filesystem::path const & data_path);
std::string get_qtwallet_toml_config_path (std::filesystem::path const & data_path);
std::string get_tls_toml_config_path (std::filesystem::path const & data_path);

/** Checks if we are running inside a valgrind instance */
bool running_within_valgrind ();

/** Checks if we are running with instrumentation that significantly affects memory consumption and can cause large virtual memory allocations to fail
	Returns true if running within Valgrind or with ThreadSanitizer tooling*/
bool memory_intensive_instrumentation ();

/** Check if we're running with instrumentation that can greatly affect performance
	Returns true if running within Valgrind or with ThreadSanitizer tooling*/
bool slow_instrumentation ();

/** Set the active network to the dev network */
void force_nano_dev_network ();

/** Checks that we are running in test mode */
bool is_dev_run ();

size_t queue_warning_threshold ();
size_t ledger_thread_stack_size ();
size_t ledger_max_rollback_depth ();
}

namespace nano
{
/**
 * Attempt to read a configuration file from specified directory. Returns empty tomlconfig if nothing is found.
 * @throws std::runtime_error with error code if the file or overrides are not valid toml
 */
nano::tomlconfig load_toml_file (const std::filesystem::path & config_filename, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides);

/**
 * Attempt to read a configuration file from specified directory. Returns fallback config if nothing is found.
 * @throws std::runtime_error with error code if the file or overrides are not valid toml or deserialization fails
 */
template <typename T>
T load_config_file (T fallback, const std::filesystem::path & config_filename, const std::filesystem::path & data_path, const std::vector<std::string> & config_overrides)
{
	auto toml = load_toml_file (config_filename, data_path, config_overrides);

	T config = fallback;
	auto error = config.deserialize_toml (toml);
	if (error)
	{
		throw std::runtime_error (error.get_message ());
	}
	return config;
}
}
