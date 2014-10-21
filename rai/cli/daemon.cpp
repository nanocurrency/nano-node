#include <rai/cli/daemon.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <fstream>
#include <thread>

rai_daemon::daemon_config::daemon_config () :
peering_port (24000),
rpc_enable (false),
rpc_port (25000),
rpc_enable_control (false)
{
    bootstrap_peers.push_back ("rai.raiblocks.net");
}

void rai_daemon::daemon_config::serialize (std::ostream & output_a)
{
    boost::property_tree::ptree tree;
    tree.put ("peering_port", std::to_string (peering_port));
    boost::property_tree::ptree bootstrap_peers_l;
    for (auto i (bootstrap_peers.begin ()), n (bootstrap_peers.end ()); i != n; ++i)
    {
        boost::property_tree::ptree entry;
        entry.put ("", *i);
        bootstrap_peers_l.push_back (std::make_pair ("", entry));
    }
    tree.add_child ("bootstrap_peers", bootstrap_peers_l);
    tree.put ("rpc_port", std::to_string (rpc_port));
    tree.put ("rpc_enable", rpc_enable);
    tree.put ("rpc_enable_control", rpc_enable_control);
    boost::property_tree::write_json (output_a, tree);
}

rai_daemon::daemon_config::daemon_config (bool & error_a, std::istream & input_a)
{
    error_a = false;
    boost::property_tree::ptree tree;
    try
    {
        boost::property_tree::read_json (input_a, tree);
        auto peering_port_l (tree.get <std::string> ("peering_port"));
        auto rpc_port_l (tree.get <std::string> ("rpc_port"));
        rpc_enable = tree.get <bool> ("rpc_enable");
        rpc_enable_control = tree.get <bool> ("rpc_enable_control");
        auto bootstrap_peers_l (tree.get_child ("bootstrap_peers"));
        for (auto i (bootstrap_peers_l.begin ()), n (bootstrap_peers_l.end ()); i != n; ++i)
        {
            auto bootstrap_peer (i->second.get <std::string> (""));
            bootstrap_peers.push_back (bootstrap_peer);
        }
        try
        {
            peering_port = std::stoul (peering_port_l);
            rpc_port = std::stoul (rpc_port_l);
            error_a = peering_port > std::numeric_limits <uint16_t>::max () || rpc_port > std::numeric_limits <uint16_t>::max ();
        }
        catch (std::logic_error const &)
        {
            error_a = true;
        }
    }
    catch (std::runtime_error const &)
    {
        std::cout << "Error parsing config file" << std::endl;
        error_a = true;
    }
}

rai_daemon::daemon::daemon ()
{
}

void rai_daemon::daemon::run ()
{
    auto working (boost::filesystem::current_path ());
    auto config_error (false);
    rai_daemon::daemon_config config;
    auto config_path ((working / "config.json").string ());
    std::ifstream config_file;
    config_file.open (config_path);
    if (!config_file.fail ())
    {
        config = rai_daemon::daemon_config (config_error, config_file);
    }
    else
    {
        std::ofstream config_file;
        config_file.open (config_path);
        if (!config_file.fail ())
        {
            config.serialize (config_file);
        }
    }
    if (!config_error)
    {
        auto service (boost::make_shared <boost::asio::io_service> ());
        auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
        rai::processor_service processor;
        rai::client_init init;
        auto client (std::make_shared <rai::client> (init, service, config.peering_port,  working / "data", processor, rai::genesis_address));
        assert (!init.error ());
		client->start ();
		rai::rpc rpc (service, pool, config.rpc_port, *client, config.rpc_enable_control);
        if (config.rpc_enable)
        {
            rpc.start ();
        }
        std::thread network_thread ([&service] ()
            {
                try
                {
                    service->run ();
                }
                catch (...)
                {
                    assert (false);
                }
            });
        std::thread processor_thread ([&processor] ()
            {
                try
                {
                    processor.run ();
                }
                catch (...)
                {
                    assert (false);
                }
            });
        network_thread.join ();
        processor_thread.join ();
    }
}
