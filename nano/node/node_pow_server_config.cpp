#include <nano/lib/tomlconfig.hpp>
#include <nano/node/node_pow_server_config.hpp>

nano::error nano::node_pow_server_config::serialize_toml (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Value is currently not in use. Enable or disable starting Nano PoW Server as a child process.\ntype:bool");
	toml.put ("nano_pow_server_path", pow_server_path, "Value is currently not in use. Path to the nano_pow_server executable.\ntype:string,path");
	return toml.get_error ();
}

nano::error nano::node_pow_server_config::deserialize_toml (nano::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<std::string> ("nano_pow_server_path", pow_server_path);

	return toml.get_error ();
}
