#pragma once

#include <chrono>
#include <cstddef>

namespace galileo
{
// Network variants with different genesis blocks and network parameters
enum class galileo_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	galileo_test_network,
	// Normal work parameters, secret beta genesis key, beta IP ports
	galileo_beta_network,
	// Normal work parameters, secret live key, live IP ports
	galileo_live_network
};
galileo::galileo_networks const galileo_network = galileo_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
