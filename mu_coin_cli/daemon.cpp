#include <mu_coin_cli/daemon.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <fstream>
#include <thread>

rai_daemon::daemon_config::daemon_config () :
peering_port (24000),
rpc_port (25000)
{
}

void rai_daemon::daemon_config::serialize (std::ostream & output_a)
{
    boost::property_tree::ptree tree;
    tree.put ("peering_port", std::to_string (peering_port));
    tree.put ("rpc_port", std::to_string (rpc_port));
    boost::property_tree::write_json (output_a, tree);
}

bool rai_daemon::daemon_config::deserialize (std::istream & input_a)
{
    auto result (false);
    boost::property_tree::ptree tree;
    try
    {
        boost::property_tree::read_json (input_a, tree);
        auto peering_port_l (tree.get <std::string> ("peering_port"));
        auto rpc_port_l (tree.get <std::string> ("rpc_port"));
        try
        {
            peering_port = std::stoul (peering_port_l);
            rpc_port = std::stoul (rpc_port_l);
            result = peering_port > std::numeric_limits <uint16_t>::max () || rpc_port > std::numeric_limits <uint16_t>::max ();
        }
        catch (std::logic_error const &)
        {
            result = true;
        }
    }
    catch (std::runtime_error const &)
    {
        std::cout << "Error parsing config file" << std::endl;
        result = true;
    }
    return result;
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
        config_error = config.deserialize (config_file);
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
        auto client (std::make_shared <rai::client> (service, config.peering_port,  working / "data", processor, rai::genesis_address));
		client->start ();
		rai::rpc rpc (service, pool, config.rpc_port, *client, std::unordered_set <rai::uint256_union> ());
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