#pragma once

#include <nano/lib/assert.hpp>
#include <nano/lib/common.hpp>
#include <nano/lib/networks.hpp>
#include <nano/lib/tomlconfig.hpp>

#include <boost/config.hpp>
#include <boost/version.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std::chrono_literals;

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

/*
 * Platform info
 */
namespace nano
{
consteval bool is_windows_build ()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

consteval bool is_macos_build ()
{
#ifdef __APPLE__
	return true;
#else
	return false;
#endif
}

consteval bool is_linux_build ()
{
#ifdef __linux__
	return true;
#else
	return false;
#endif
}
}

namespace nano
{
uint16_t test_node_port ();
uint16_t test_rpc_port ();
uint16_t test_ipc_port ();
uint16_t test_websocket_port ();
std::array<uint8_t, 2> test_magic_number ();
uint32_t test_scan_wallet_reps_delay (); // How often to scan for representatives in local wallet, in milliseconds

// Configuration file names
constexpr std::string_view node_config_filename{ "config-node.toml" };
constexpr std::string_view rpc_config_filename{ "config-rpc.toml" };
constexpr std::string_view log_config_filename{ "config-log.toml" };
constexpr std::string_view access_config_filename{ "config-access.toml" };
constexpr std::string_view qtwallet_config_filename{ "config-qtwallet.toml" };
constexpr std::string_view tls_config_filename{ "config-tls.toml" };

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

/**
 * Queue backlog size above which processing components log a periodic warning;
 * NANO_QUEUE_WARNING_THRESHOLD overrides
 */
size_t queue_warning_threshold ();

/**
 * Stack size for threads executing ledger operations, sized for deep rollback recursion;
 * NANO_LEDGER_THREAD_STACK_SIZE overrides
 */
size_t ledger_thread_stack_size ();
/**
 * Default cap on cascading rollback depth before the rollback is aborted as an error;
 * NANO_MAX_ROLLBACK_DEPTH overrides
 */
size_t ledger_max_rollback_depth ();
/**
 * Entries per transaction for bulk ledger upgrade walks (schema upgrades, index population);
 * NANO_LEDGER_UPGRADE_BATCH_SIZE overrides, small under dev runs to exercise refresh paths
 */
size_t ledger_upgrade_batch_size ();

/**
 * Database backend used when the node configuration does not specify one;
 * NANO_BACKEND overrides
 */
nano::database_backend default_database_backend ();
}

namespace nano
{
/**
 * Reads the configuration file `filename` from `data_path` into `toml` and applies `overrides` on top of it.
 * Overrides are `key=value` entries and take precedence over the file.
 * A missing file is not an error and is never created; the result then holds only the overrides.
 * Reports on stderr whether the file was found.
 * A returned error names `filename` in its message.
 */
nano::error read_config_file (nano::tomlconfig & toml, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {});

/** Prefixes the message of a failed \p error with \p filename, so that the report names the file it is about */
nano::error prefix_config_error (nano::error error, std::string_view filename);

/** Reads the configuration file as above and deserializes it into `config`, which keeps its current values for every key the file does not mention */
template <typename T>
nano::error read_config_file (T & config, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	nano::tomlconfig toml;
	if (auto error = read_config_file (toml, filename, data_path, overrides))
	{
		return error;
	}
	return prefix_config_error (config.deserialize_toml (toml), filename);
}

/**
 * Reads the configuration file on top of `config` as `read_config_file` and returns the result.
 * @throws std::runtime_error if the file or overrides are not valid toml or deserialization fails
 */
template <typename T>
T load_config_file (T config, std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	if (auto error = read_config_file (config, filename, data_path, overrides))
	{
		throw std::runtime_error (error.get_message ());
	}
	return config;
}

/** As above, on top of a default-constructed `T` */
template <typename T>
T load_config_file (std::string_view filename, std::filesystem::path const & data_path, std::vector<std::string> const & overrides = {})
{
	return load_config_file (T{}, filename, data_path, overrides);
}
}
