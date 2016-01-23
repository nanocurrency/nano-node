#include <rai/rai_node/daemon.hpp>

#include <rai/node/working.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <fstream>

rai_daemon::daemon_config::daemon_config () :
rpc_enable (false)
{
}

void rai_daemon::daemon_config::serialize_json (boost::property_tree::ptree & tree_a)
{
	tree_a.put ("rpc_enable", rpc_enable);
	boost::property_tree::ptree rpc_l;
	rpc.serialize_json (rpc_l);
	tree_a.add_child ("rpc", rpc_l);
	boost::property_tree::ptree node_l;
	node.serialize_json (node_l);
	tree_a.add_child ("node", node_l);
}

bool rai_daemon::daemon_config::deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
{
    auto error (false);
	try
	{
		if (!tree_a.empty ())
		{
			rpc_enable = tree_a.get <bool> ("rpc_enable");
			auto node_l (tree_a.get_child ("node"));
			error |= node.deserialize_json (upgraded_a, node_l);
			auto rpc_l (tree_a.get_child ("rpc"));
			error |= rpc.deserialize_json (rpc_l);
		}
		else
		{
			upgraded_a = true;
			serialize_json (tree_a);
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void rai_daemon::daemon::run ()
{
    auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
    rai_daemon::daemon_config config;
    auto config_path ((working / "config.json").string ());
    std::fstream config_file;
	config_file.open (config_path, std::ios_base::out);
	config_file.close ();
	config_file.open (config_path, std::ios_base::in | std::ios_base::out);
    std::unique_ptr <rai::thread_runner> runner;
    if (!config_file.fail ())
    {
		auto error (rai::fetch_object (config, config_file));
		if (!error)
		{
			auto service (boost::make_shared <boost::asio::io_service> ());
			auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
			rai::work_pool work;
			rai::processor_service processor;
			rai::node_init init;
			auto node (std::make_shared <rai::node> (init, service, working, processor, config.node, work));
			if (!init.error ())
			{
				node->start ();
				rai::rpc rpc (service, pool, *node, config.rpc);
				if (config.rpc_enable)
				{
					rpc.start ();
				}
				runner.reset (new rai::thread_runner (*service, processor));
				runner->join ();
			}
			else
			{
				std::cerr << "Error initializing node\n";
			}
		}
		else
		{
			std::cerr << "Error deserializing config\n";
		}
    }
    else
    {
        std::cerr << "Error loading configuration\n";
    }
}
