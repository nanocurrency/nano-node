#pragma once

#include <celerix/lib/config.hpp>
#include <celerix/lib/constants.hpp>

#include <chrono>

namespace celerix::transport
{
class tcp_config
{
public:
	explicit tcp_config (celerix::network_constants const & network)
	{
		if (network.is_dev_network ())
		{
			max_inbound_connections = 128;
			max_outbound_connections = 128;
			max_attempts = 128;
			max_attempts_per_ip = 128;
			connect_timeout = std::chrono::seconds{ 5 };
		}
	}

public:
	celerix::error deserialize (celerix::tomlconfig &);
	celerix::error serialize (celerix::tomlconfig &) const;

public:
	size_t max_inbound_connections{ 2048 };
	size_t max_outbound_connections{ 2048 };
	size_t max_attempts{ 60 };
	size_t max_attempts_per_ip{ 1 };
	std::chrono::seconds connect_timeout{ 60 };
	std::chrono::seconds handshake_timeout{ 30 };
	std::chrono::seconds io_timeout{ 30 };
};
}