#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nano
{
/**
 * Network variants with different genesis blocks and network parameters
 */
enum class network_type : uint16_t
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

std::string_view to_string (nano::network_type);
std::optional<nano::network_type> parse_network (std::string const &);

/**
 * Current active network. Defaults to the compile-time option, but can be overridden.
 */
nano::network_type get_active_network ();

/**
 * Optionally called on startup to override the global active network.
 * If not called, the compile-time option will be used.
 */
void set_active_network (nano::network_type network);
}
