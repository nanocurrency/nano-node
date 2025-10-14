#include <nano/lib/assert.hpp>
#include <nano/lib/networks.hpp>

namespace
{
// Initial value is ACTIVE_NETWORK compile flag, but can be overridden by CLI/API
nano::network_type & active_network_singleton ()
{
	static nano::network_type instance{ nano::network_type::ACTIVE_NETWORK };
	return instance;
}
}

namespace nano
{
nano::network_type get_active_network ()
{
	return active_network_singleton ();
}

void set_active_network (nano::network_type network)
{
	active_network_singleton () = network;
}

std::string_view to_string (nano::network_type network)
{
	switch (network)
	{
		case nano::network_type::nano_beta_network:
			return "beta";
		case nano::network_type::nano_dev_network:
			return "dev";
		case nano::network_type::nano_live_network:
			return "live";
		case nano::network_type::nano_test_network:
			return "test";
		case nano::network_type::invalid:
			return "invalid";
	}
	release_assert (false, "invalid network");
}

std::optional<nano::network_type> parse_network (std::string const & network_name)
{
	if (network_name == "live")
	{
		return nano::network_type::nano_live_network;
	}
	if (network_name == "beta")
	{
		return nano::network_type::nano_beta_network;
	}
	if (network_name == "dev")
	{
		return nano::network_type::nano_dev_network;
	}
	if (network_name == "test")
	{
		return nano::network_type::nano_test_network;
	}
	return std::nullopt;
}
}
