#include <nano/lib/config.hpp>
#include <nano/node/daemonconfig.hpp>

nano::daemon_config::daemon_config (boost::filesystem::path const & data_path) :
rpc_path (get_default_rpc_filepath ()),
data_path (data_path)
{
}

nano::error nano::daemon_config::serialize_json (nano::jsonconfig & json)
{
	json.put ("version", json_version ());
	json.put ("rpc_enable", rpc_enable);
	json.put ("rpc_path", rpc_path);

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

			bool enable_sign_hash = false;
			// If rpc exists then copy enable_sign_hash value now as the upgrade can remove it for earlier versions
			if (version_l <= 2)
			{
				auto rpc = json.get_optional_child ("rpc");
				if (rpc)
				{
					auto enable_sign_hash_json = rpc->get_optional<bool> ("enable_sign_hash");
					if (enable_sign_hash_json)
					{
						enable_sign_hash = *enable_sign_hash_json;
					}
				}
			}

			upgraded_a |= upgrade_json (version_l, json);

			json.get_optional<bool> ("rpc_enable", rpc_enable);
			json.get_optional<std::string> ("rpc_path", rpc_path);

			auto node_l (json.get_required_child ("node"));
			if (!json.get_error ())
			{
				node.deserialize_json (upgraded_a, node_l, rpc_enable, enable_sign_hash);
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
			migrate_rpc_config (json, data_path);
			rpc_path = get_default_rpc_filepath ();
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
	auto error (json.read_and_update (config_a, config_path));
	nano::set_secure_perm_file (config_path, error_chmod);
	return error;
}
}
