#pragma once

#include <chrono>
#include <cstddef>

namespace rai
{
// Network variants with different genesis blocks and network parameters
enum class banano_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	banano_test_network,
	// Normal work parameters, secret beta genesis key, beta IP ports
	banano_beta_network,
	// Normal work parameters, secret live key, live IP ports
	banano_live_network
};
rai::banano_networks const banano_network = banano_networks::ACTIVE_NETWORK;
std::chrono::milliseconds const transaction_timeout = std::chrono::milliseconds (1000);
}
