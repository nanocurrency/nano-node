#pragma once

#include <celerix/lib/errors.hpp>
#include <celerix/node/node_rpc_config.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/openclconfig.hpp>

#include <vector>

namespace celerix
{
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (std::filesystem::path const & data_path, celerix::network_params & network_params);
	celerix::error deserialize_toml (celerix::tomlconfig &);
	celerix::error serialize_toml (celerix::tomlconfig &);
	bool rpc_enable{ false };
	celerix::node_rpc_config rpc;
	celerix::node_config node;
	bool opencl_enable{ false };
	celerix::opencl_config opencl;
	std::filesystem::path data_path;
};

celerix::error read_node_config_toml (std::filesystem::path const &, celerix::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
}
