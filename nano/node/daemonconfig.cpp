#include <nano/lib/config.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/daemonconfig.hpp>

nano::daemon_config::daemon_config (std::filesystem::path const & data_path_a, nano::network_params & network_params) :
	node{ network_params },
	data_path{ data_path_a }
{
}

nano::error nano::daemon_config::serialize_toml (nano::tomlconfig & toml)
{
	nano::tomlconfig rpc_l;
	rpc.serialize_toml (rpc_l);
	rpc_l.doc ("enable", "Enable or disable RPC\ntype:bool");
	rpc_l.put ("enable", rpc_enable);
	toml.put_child ("rpc", rpc_l);

	nano::tomlconfig node_l;
	node.serialize_toml (node_l);
	toml.put_child ("node", node_l);

	nano::tomlconfig opencl_l;
	opencl.serialize_toml (opencl_l);
	opencl_l.doc ("enable", "Enable or disable OpenCL work generation\nIf enabled, consider freeing up CPU resources by setting [work_threads] to zero\ntype:bool");
	opencl_l.put ("enable", opencl_enable);
	toml.put_child ("opencl", opencl_l);

	return toml.get_error ();
}

nano::error nano::daemon_config::deserialize_toml (nano::tomlconfig & toml)
{
	auto rpc_l (toml.get_optional_child ("rpc"));
	if (!toml.get_error () && rpc_l)
	{
		rpc_l->get_optional<bool> ("enable", rpc_enable);
		rpc.deserialize_toml (*rpc_l);
	}

	auto node_l (toml.get_optional_child ("node"));
	if (!toml.get_error () && node_l)
	{
		node.deserialize_toml (*node_l);
	}

	auto opencl_l (toml.get_optional_child ("opencl"));
	if (!toml.get_error () && opencl_l)
	{
		opencl_l->get_optional<bool> ("enable", opencl_enable);
		opencl.deserialize_toml (*opencl_l);
	}

	return toml.get_error ();
}

nano::error nano::daemon_config::validate () const
{
	return node.validate ();
}

nano::daemon_config nano::load_daemon_config (std::filesystem::path const & data_path, nano::network_params & network_params, std::vector<std::string> const & config_overrides)
{
	return nano::load_config_file (nano::daemon_config{ data_path, network_params }, nano::node_config_filename, data_path, config_overrides);
}

nano::daemon_config nano::load_daemon_config (std::filesystem::path const & data_path, nano::network_params & network_params, nano::node_flags const & flags)
{
	auto config = load_daemon_config (data_path, network_params, flags.config_overrides);
	if (auto error = config.node.validate (flags))
	{
		throw std::runtime_error{ error.get_message () };
	}
	return config;
}

nano::node_config nano::load_node_config (std::filesystem::path const & data_path, nano::network_params & network_params, std::vector<std::string> const & config_overrides)
{
	return load_daemon_config (data_path, network_params, config_overrides).node;
}
