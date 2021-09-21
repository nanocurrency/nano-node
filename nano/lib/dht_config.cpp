#include <nano/lib/config.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/dht/dht.hpp>

nano::error nano::dht_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Whether to use Disk-based Hash Table backend for the unchecked blocks table.\ntype:bool");
	return toml.get_error ();
}

nano::error nano::dht_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	return toml.get_error ();
}

bool nano::dht_config::using_dht_in_tests ()
{
	auto use_dht_str = std::getenv ("TEST_USE_DHT");
	return use_dht_str && (boost::lexical_cast<int> (use_dht_str) == 1);
}
