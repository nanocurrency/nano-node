#pragma once

#include <nano/lib/config.hpp>
#include <nano/lib/constants.hpp>

#include <chrono>

namespace nano::transport
{
class tcp_config
{
public:
	explicit tcp_config (nano::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			max_inbound_connections = 128;
			max_outbound_connections = 128;
			max_attempts = 128;
			max_attempts_per_ip = 128;
			connect_timeout = 5s;
			checkup_interval = 1s;
			reconnect_cooldown = 1s;
		}
	}

public:
	nano::error deserialize (nano::tomlconfig &);
	nano::error serialize (nano::tomlconfig &) const;

public:
	size_t max_inbound_connections{ 2048 }; // Accepted connections open at once
	size_t max_outbound_connections{ 2048 }; // Initiated connections open at once
	size_t max_attempts{ 60 }; // Outbound connects in flight at once
	size_t max_attempts_per_ip{ 1 }; // Outbound connects in flight to one IP
	std::chrono::seconds connect_timeout{ 30 }; // How long an outbound connect may take before it is cancelled
	std::chrono::seconds handshake_timeout{ 30 }; // How long a connection may stay without a completed handshake
	std::chrono::seconds io_timeout{ 30 }; // How long a single read or write may stall, 0 disables
	std::chrono::seconds silent_timeout{ 30 }; // How long a connection may carry no traffic at all, 0 disables
	std::chrono::seconds reconnect_cooldown{ 60 }; // Time a closed connection keeps counting toward the per IP and per subnetwork limits, 0 disables
	std::chrono::seconds checkup_interval{ 5 }; // How often a socket checks its timeouts
};
}