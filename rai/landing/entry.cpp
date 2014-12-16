#include <rai/core/core.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <fstream>
#include <thread>

namespace rai
{
namespace landing
{
    uint64_t minutes_since_epoch ()
    {
        return std::chrono::duration_cast <std::chrono::minutes> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
    }
    class config
    {
    public:
        config () :
        start (minutes_since_epoch ()),
        last (start),
        peering_port (24000)
        {
            bootstrap_peers.push_back ("rai.raiblocks.net");
        }
        config (bool & error_a, std::istream & stream_a)
        {
            error_a = false;
            boost::property_tree::ptree tree;
            try
            {
                boost::property_tree::read_json (stream_a, tree);
                auto start_l (tree.get <std::string> ("start"));
                auto last_l (tree.get <std::string> ("last"));
                auto peering_port_l (tree.get <std::string> ("peering_port"));
                auto distribution_account_l (tree.get <std::string> ("distribution_account"));
                auto bootstrap_peers_l (tree.get_child ("bootstrap_peers"));
                bootstrap_peers.clear ();
                for (auto i (bootstrap_peers_l.begin ()), n (bootstrap_peers_l.end ()); i != n; ++i)
                {
                    auto bootstrap_peer (i->second.get <std::string> (""));
                    bootstrap_peers.push_back (bootstrap_peer);
                }
                try
                {
                    start = std::stoull (start_l);
                    last = std::stoull (last_l);
                    peering_port = std::stoul (peering_port_l);
                    error_a = peering_port > std::numeric_limits <uint16_t>::max ();
                }
                catch (std::logic_error const &)
                {
                    error_a = true;
                }
                error_a = error_a | distribution_account.decode_base58check (distribution_account_l);
            }
            catch (std::runtime_error const &)
            {
                error_a = true;
            }
        }
        void serialize (std::ostream & stream_a) const
        {
            boost::property_tree::ptree tree;
            tree.put ("start", std::to_string (start));
            tree.put ("last", std::to_string (last));
            tree.put ("peering_port", std::to_string (peering_port));
            std::string check;
            distribution_account.encode_base58check (check);
            tree.put ("distribution_account", check);
            boost::property_tree::ptree bootstrap_peers_l;
            for (auto i (bootstrap_peers.begin ()), n (bootstrap_peers.end ()); i != n; ++i)
            {
                boost::property_tree::ptree entry;
                entry.put ("", *i);
                bootstrap_peers_l.push_back (std::make_pair ("", entry));
            }
            tree.add_child ("bootstrap_peers", bootstrap_peers_l);
            boost::property_tree::write_json (stream_a, tree);
        }
        std::vector <std::string> bootstrap_peers;
        rai::account distribution_account;
        uint64_t start;
        uint64_t last;
        uint16_t peering_port;
    };
    uint64_t distribution_amount (uint64_t interval)
    {
        uint64_t minutes_per_year (60 * 24 * 365);
        uint64_t result;
        if (interval < minutes_per_year * 1)
        {
            result = 3237084921241;
        }
        else if (interval < minutes_per_year * 2)
        {
            result = 1618542460620;
        }
        else if (interval < minutes_per_year * 3)
        {
            result = 809271230310;
        }
        else if (interval < minutes_per_year * 4)
        {
            result = 404635615155;
        }
        else if (interval < minutes_per_year * 5)
        {
            result = 404635615155;
        }
        else
        {
            result = 0;
        }
        return result;
    }
    void write_config (boost::filesystem::path working_path_a, rai::landing::config const & config_a)
    {
        auto config_path ((working_path_a / "config.json").string ());
        std::ofstream config_file;
        config_file.open (config_path);
        if (!config_file.fail ())
        {
            config_a.serialize (config_file);
        }
    }
    void distribute (rai::client & client_a, rai::landing::config & config_a, boost::filesystem::path working_path_a)
    {
        auto now (rai::landing::minutes_since_epoch ());
        auto error (false);
        while (!error && config_a.last < now)
        {
            ++config_a.last;
            auto amount (distribution_amount (config_a.last - config_a.start));
            error = client_a.send (config_a.distribution_account, rai::scale_up (amount));
            if (!error)
            {
                std::cout << boost::str (boost::format ("Successfully distributed %1%\n") % amount);
                write_config (working_path_a, config_a);
            }
            else
            {
                std::cout << "Error while sending distribution\n";
            }
        }
        std::cout << "Waiting for next distribution cycle\n";
        client_a.service.add (std::chrono::system_clock::now () + std::chrono::minutes (1), [&client_a, &config_a, working_path_a] () {rai::landing::distribute (client_a, config_a, working_path_a);});
    }
    rai::landing::config read_config (bool & config_error, boost::filesystem::path working_path_a)
    {
        rai::landing::config result;
        auto config_path ((working_path_a / "config.json").string ());
        std::ifstream config_file;
        config_file.open (config_path);
        if (!config_file.fail ())
        {
            result = rai::landing::config (config_error, config_file);
        }
        else
        {
            config_error = true;
            write_config (working_path_a, result);
        }
        return result;
    }
}
}

int main ()
{
    auto working (boost::filesystem::current_path ());
    auto config_error (false);
    rai::landing::config config (rai::landing::read_config (config_error, working));
    if (!config_error)
    {
        rai::client_init init;
        auto service (boost::make_shared <boost::asio::io_service> ());
        rai::processor_service processor;
        auto client (std::make_shared <rai::client> (init, service, config.peering_port, working, processor));
        if (!init.error ())
        {
            client->bootstrap_peers = config.bootstrap_peers;
            client->start ();
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
            auto now (rai::landing::minutes_since_epoch ());
            if (now - config.last > 0)
            {
                std::cout << boost::str (boost::format ("The last distribution was %1% minutes ago\n") % (now - config.last));
            }
            else
            {
                std::cout << boost::str (boost::format ("Distribution will begin in %1% minutes\n") % (config.last - now));
            }
            auto wallet_entry (client->wallet.store.begin ());
            if (wallet_entry == client->wallet.store.end ())
            {
                rai::keypair key;
                client->wallet.store.insert (key.prv);
                wallet_entry = client->wallet.store.begin ();
            }
            assert (wallet_entry != client->wallet.store.end ());
            std::string landing_account;
            wallet_entry->first.encode_base58check (landing_account);
            ++wallet_entry;
            assert (wallet_entry == client->wallet.store.end ());
            std::cout << boost::str (boost::format ("Landing account: %1%\n") % landing_account);
            std::cout << "Type a line to start\n";
            std::string line;
            std::cin >> line;
            rai::landing::distribute (*client, config, working);
            network_thread.join ();
            processor_thread.join ();
        }
        else
        {
            std::cerr << "Error initializing client\n";
        }
    }
    else
    {
        std::cerr << "Error loading configuration\n";
    }
}