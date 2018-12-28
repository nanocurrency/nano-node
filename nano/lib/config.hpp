#pragma once

#include <chrono>
#include <cstddef>

namespace rai
{
/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used for comparison; do not change.
 */
enum class rai_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	rai_test_network = 0,
	// Normal work parameters, secret beta genesis key, beta IP ports
	rai_beta_network = 1,
	// Normal work parameters, secret live key, live IP ports
	rai_live_network = 2
};
rai::rai_networks const rai_network = rai_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
