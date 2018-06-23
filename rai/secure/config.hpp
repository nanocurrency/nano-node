#pragma once

#include <chrono>
#include <cstddef>

namespace rai
{
// Network variants with different genesis blocks and network parameters
enum class rai_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	rai_test_network,
	// Normal work parameters, secret beta genesis key, beta IP ports
	rai_beta_network,
	// Normal work parameters, secret live key, live IP ports
	rai_live_network
};
rai::rai_networks const rai_network = rai_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
