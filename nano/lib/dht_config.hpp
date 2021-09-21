#pragma once

#include <nano/lib/errors.hpp>

namespace nano
{
class tomlconfig;

/** Configuration options for Disk-based Hash Table */
class dht_config final
{
public:
	dht_config () :
		enable{ using_dht_in_tests () }
	{
	}

	nano::error serialize_toml (nano::tomlconfig & toml_a) const;
	nano::error deserialize_toml (nano::tomlconfig & toml_a);

	/** To use Disk-based Hash Table in tests make sure the environment variable TEST_USE_DHT=1 is set */
	static bool using_dht_in_tests ();

	bool enable{ false };
};
}
