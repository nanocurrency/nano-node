#include <celerix/lib/config.hpp>
#include <celerix/lib/jsonconfig.hpp>
#include <celerix/lib/tomlconfig.hpp>
#include <celerix/node/daemonconfig.hpp>

#include <sstream>
#include <vector>

celerix::daemon_config::daemon_config (std::filesystem::path const & data_path_a, celerix::network_params & network_params) :
	node{ network_params },
	data_path{ data_path_a }
{
}

celerix::error celerix::daemon_config::serialize_toml (celerix::tomlconfig & toml)
{
	celerix::tomlconfig rpc_l;
	rpc.serialize_toml (rpc_l);
	rpc_l.doc ("enable", "Enable or disable RPC\ntype:bool");
	rpc_l.put ("enable", rpc_enable);
	toml.put_child ("rpc", rpc_l);

	celerix::tomlconfig node_l;
	node.serialize_toml (node_l);
	celerix::tomlconfig node (node_l);
	toml.put_child ("node", node);

	celerix::tomlconfig opencl_l;
	opencl.serialize_toml (opencl_l);
	opencl_l.doc ("enable", "Enable or disable OpenCL work generation\nIf enabled, consider freeing up CPU resources by setting [work_threads] to zero\ntype:bool");
	opencl_l.put ("enable", opencl_enable);
	toml.put_child ("opencl", opencl_l);

	return toml.get_error ();
}

celerix::error celerix::daemon_config::deserialize_toml (celerix::tomlconfig & toml)
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

celerix::error celerix::read_node_config_toml (std::filesystem::path const & data_path_a, celerix::daemon_config & config_a, std::vector<std::string> const & config_overrides)
{
	celerix::error error;
	auto toml_config_path = celerix::get_node_toml_config_path (data_path_a);
	auto toml_qt_config_path = celerix::get_qtwallet_toml_config_path (data_path_a);

	// Parse and deserialize
	celerix::tomlconfig toml;

	std::stringstream config_overrides_stream;
	for (auto const & entry : config_overrides)
	{
		config_overrides_stream << entry << std::endl;
	}
	config_overrides_stream << std::endl;

	// Make sure we don't create an empty toml file if it doesn't exist. Running without a toml file is the default.
	if (!error)
	{
		if (std::filesystem::exists (toml_config_path))
		{
			error = toml.read (config_overrides_stream, toml_config_path);
		}
		else
		{
			error = toml.read (config_overrides_stream);
		}
	}

	if (!error)
	{
		error = config_a.deserialize_toml (toml);
	}

	return error;
}
