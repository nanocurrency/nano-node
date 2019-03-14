#include <nano/lib/config.hpp>
#include <nano/node/daemonconfig.hpp>

nano::daemon_config::daemon_config () :
opencl_enable (false)
{
}

nano::error nano::daemon_config::serialize_json (nano::jsonconfig & json)
{
	json.put ("version", json_version ());

	nano::jsonconfig node_l;
	node.serialize_json (node_l);
	nano::jsonconfig node (node_l);
	json.put_child ("node", node);

	json.put ("opencl_enable", opencl_enable);
	nano::jsonconfig opencl_l;
	opencl.serialize_json (opencl_l);
	json.put_child ("opencl", opencl_l);
	return json.get_error ();
}

nano::error nano::daemon_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json)
{
	try
	{
		if (!json.empty ())
		{
			int version_l;
			json.get_optional<int> ("version", version_l);
			upgraded_a |= upgrade_json (version_l, json);

			auto node_l (json.get_required_child ("node"));
			if (!json.get_error ())
			{
				node.deserialize_json (upgraded_a, node_l);
			}
			if (!json.get_error ())
			{
				json.get_required<bool> ("opencl_enable", opencl_enable);
				auto opencl_l (json.get_required_child ("opencl"));
				if (!json.get_error ())
				{
					opencl.deserialize_json (opencl_l);
				}
			}
		}
		else
		{
			upgraded_a = true;
			serialize_json (json);
		}
	}
	catch (std::runtime_error const & ex)
	{
		json.get_error () = ex;
	}
	return json.get_error ();
}

bool nano::daemon_config::upgrade_json (unsigned version_a, nano::jsonconfig & json)
{
	json.put ("version", json_version ());
	auto upgraded_l (false);
	switch (version_a)
	{
		case 1:
		{
			bool opencl_enable_l;
			json.get_optional<bool> ("opencl_enable", opencl_enable_l);
			if (!opencl_enable_l)
			{
				json.put ("opencl_enable", false);
			}
			auto opencl_l (json.get_optional_child ("opencl"));
			if (!opencl_l)
			{
				nano::jsonconfig opencl_l;
				opencl.serialize_json (opencl_l);
				json.put_child ("opencl", opencl_l);
			}
			upgraded_l = true;
		}
		case 2:
			// RPC config is no longer relevant so remove it. Migrating is done elsewhere
			json.erase ("rpc_enable");
			json.erase ("rpc");
			upgraded_l = true;
		case 3:
			break;
		default:
			throw std::runtime_error ("Unknown daemon_config version");
	}
	return upgraded_l;
}

namespace nano
{
nano::error read_and_update_daemon_config (boost::filesystem::path const & data_path, nano::daemon_config & config_a)
{
	boost::system::error_code error_chmod;
	nano::jsonconfig json;
	auto config_path = nano::get_config_path (data_path);

	{
		// Get version if 2 then copy rpc across
		auto error (json.read (config_a, config_path));
		if (!error)
		{
			unsigned version = 1;
			json.get_required<unsigned> ("version", version);
			if (version <= 2)
			{
				auto rpc_l (json.get_required_child ("rpc"));

				// The value is not migrated to the ipc_config
				rpc_l.erase ("enable_sign_hash");

				auto node_l (json.get_required_child ("node"));
				auto ipc_l (node_l.get_optional_child ("ipc"));
				if (ipc_l)
				{
					nano::ipc::ipc_config ipc_config;
					auto upgraded (false);
					auto err1 = ipc_config.deserialize_json (upgraded, *ipc_l);
					if (!err1)
					{
						// Add IPC to RPC
						if (ipc_config.transport_tcp.enabled)
						{
							rpc_l.put ("ipc_port", ipc_config.transport_tcp.port);
						}
						if (ipc_config.transport_domain.enabled)
						{
							rpc_l.put ("ipc_path", ipc_config.transport_domain.path);
						}
					}
				}

				auto io_threads = node_l.get_optional<unsigned> ("io_threads");
				if (io_threads)
				{
					rpc_l.put ("io_threads", std::to_string (*io_threads));
				}

				nano::jsonconfig rpc_json;
				auto rpc_config_path = nano::get_rpc_config_path (data_path);

				auto rpc_error (rpc_json.read (config_a, rpc_config_path));
				if (rpc_error || rpc_json.empty ())
				{
					// Migrate RPC info across
					rpc_l.write (rpc_config_path);
				}
			}
		}
	}

	auto error (json.read_and_update (config_a, config_path));
	nano::set_secure_perm_file (config_path, error_chmod);
	return error;
}
}
