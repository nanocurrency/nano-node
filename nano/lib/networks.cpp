#include <nano/lib/assert.hpp>
#include <nano/lib/networks.hpp>

namespace
{
// Initial value is ACTIVE_NETWORK compile flag, but can be overridden by CLI/API
nano::networks & active_network_singleton ()
{
	static nano::networks instance{ nano::networks::ACTIVE_NETWORK };
	return instance;
}
}

namespace nano
{
nano::networks get_active_network ()
{
	return active_network_singleton ();
}

void set_active_network (nano::networks network)
{
	active_network_singleton () = network;
}

std::string_view to_string (nano::networks network)
{
	switch (network)
	{
		case nano::networks::nano_beta_network:
			return "beta";
		case nano::networks::nano_dev_network:
			return "dev";
		case nano::networks::nano_live_network:
			return "live";
		case nano::networks::nano_test_network:
			return "test";
		case nano::networks::invalid:
			return "invalid";
	}
	release_assert (false, "invalid network");
}

std::optional<nano::networks> parse_network (std::string const & network_name)
{
	if (network_name == "live")
	{
		return nano::networks::nano_live_network;
	}
	if (network_name == "beta")
	{
		return nano::networks::nano_beta_network;
	}
	if (network_name == "dev")
	{
		return nano::networks::nano_dev_network;
	}
	if (network_name == "test")
	{
		return nano::networks::nano_test_network;
	}
	return std::nullopt;
}
}
