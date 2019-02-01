#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/utility.hpp>
#include <nano/nano_node/daemon.hpp>
#include <nano/node/ipc.hpp>
#include <nano/node/working.hpp>

nano_daemon::daemon_config::daemon_config () :
rpc_enable (false),
opencl_enable (false)
{
}

nano::error nano_daemon::daemon_config::serialize_json (nano::jsonconfig & json)
{
	json.put ("version", json_version ());
	json.put ("rpc_enable", rpc_enable);

	nano::jsonconfig rpc_l;
	rpc.serialize_json (rpc_l);
	json.put_child ("rpc", rpc_l);

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

nano::error nano_daemon::daemon_config::deserialize_json (bool & upgraded_a, nano::jsonconfig & json)
{
	try
	{
		if (!json.empty ())
		{
			int version_l;
			json.get_optional<int> ("version", version_l);
			upgraded_a |= upgrade_json (version_l, json);

			json.get_optional<bool> ("rpc_enable", rpc_enable);
			auto rpc_l (json.get_required_child ("rpc"));

			if (!rpc.deserialize_json (rpc_l))
			{
				auto node_l (json.get_required_child ("node"));
				if (!json.get_error ())
				{
					node.deserialize_json (upgraded_a, node_l);
				}
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

bool nano_daemon::daemon_config::upgrade_json (unsigned version_a, nano::jsonconfig & json)
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
			break;
		default:
			throw std::runtime_error ("Unknown daemon_config version");
	}
	return upgraded_l;
}

void nano_daemon::daemon::run (boost::filesystem::path const & data_path, nano::node_flags const & flags)
{
	boost::system::error_code error_chmod;
	boost::filesystem::create_directories (data_path);
	nano_daemon::daemon_config config;
	nano::set_secure_perm_directory (data_path, error_chmod);
	auto config_path ((data_path / "config.json"));
	std::unique_ptr<nano::thread_runner> runner;
	nano::jsonconfig json;
	auto error (json.read_and_update (config, config_path));
	nano::set_secure_perm_file (config_path, error_chmod);
	if (!error)
	{
		config.node.logging.init (data_path);
		boost::asio::io_context io_ctx;
		auto opencl (nano::opencl_work::create (config.opencl_enable, config.opencl, config.node.logging));
		nano::work_pool opencl_work (config.node.work_threads, opencl ? [&opencl](nano::uint256_union const & root_a) {
			return opencl->generate_work (root_a);
		}
		                                                              : std::function<boost::optional<uint64_t> (nano::uint256_union const &)> (nullptr));
		nano::alarm alarm (io_ctx);
		nano::node_init init;
		try
		{
			auto node (std::make_shared<nano::node> (init, io_ctx, data_path, alarm, config.node, opencl_work, flags));
			if (!init.error ())
			{
				node->start ();
				std::unique_ptr<nano::rpc> rpc = get_rpc (io_ctx, *node, config.rpc);
				if (rpc)
				{
					rpc->start (config.rpc_enable);
				}
				nano::ipc::ipc_server ipc (*node, *rpc);
				runner = std::make_unique<nano::thread_runner> (io_ctx, node->config.io_threads);
				runner->join ();
			}
			else
			{
				std::cerr << "Error initializing node\n";
			}
		}
		catch (const std::runtime_error & e)
		{
			std::cerr << "Error while running node (" << e.what () << ")\n";
		}
	}
	else
	{
		std::cerr << "Error deserializing config: " << error.get_message () << std::endl;
	}
}
