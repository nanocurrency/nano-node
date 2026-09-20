#pragma once

#include <nano/lib/errors.hpp>
#include <nano/node/node_rpc_config.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/openclconfig.hpp>

#include <vector>

namespace nano
{
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (std::filesystem::path const & data_path, nano::network_params & network_params);
	nano::error deserialize_toml (nano::tomlconfig &);
	nano::error serialize_toml (nano::tomlconfig &);
	nano::error validate () const;
	bool rpc_enable{ false };
	nano::node_rpc_config rpc;
	nano::node_config node;
	bool opencl_enable{ false };
	nano::opencl_config opencl;
	std::filesystem::path data_path;
};

/** Loads config-node.toml from \p data_path on top of the defaults for \p network_params; throws nano::config_error */
nano::daemon_config load_daemon_config (std::filesystem::path const & data_path, nano::network_params & network_params, std::vector<std::string> const & config_overrides = {});
/** As above with the overrides taken from \p flags, and rejects flags that conflict with the config; throws std::runtime_error */
nano::daemon_config load_daemon_config (std::filesystem::path const & data_path, nano::network_params & network_params, nano::node_flags const & flags);
/** The node section of config-node.toml, loaded as by load_daemon_config; throws nano::config_error */
nano::node_config load_node_config (std::filesystem::path const & data_path, nano::network_params & network_params, std::vector<std::string> const & config_overrides = {});
}
