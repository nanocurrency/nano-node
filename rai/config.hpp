#pragma once

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
size_t const blocks_per_transaction = rai::rai_network == rai::rai_networks::rai_test_network ? 2 : 16384;
}
