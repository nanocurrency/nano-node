#pragma once

#include <chrono>
#include <cstddef>

namespace nano
{
/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used for comparison; do not change.
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
nano::nano_networks constexpr nano_network = nano_networks::ACTIVE_NETWORK;
bool constexpr is_live_network = nano_network == nano_networks::nano_live_network;
bool constexpr is_beta_network = nano_network == nano_networks::nano_beta_network;
bool constexpr is_test_network = nano_network == nano_networks::nano_test_network;

std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
