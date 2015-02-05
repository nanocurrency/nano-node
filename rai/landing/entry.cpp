#include <rai/node.hpp>

#include <rai/working.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <fstream>
#include <thread>

namespace rai
{
namespace landing
{
    uint64_t seconds_since_epoch ()
    {
        return std::chrono::duration_cast <std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
    }
    class config
    {
    public:
        config () :
        start (seconds_since_epoch ()),
        last (start),
        peering_port (rai::network::node_port)
        {
            bootstrap_peers.push_back ("rai.raiblocks.net");
            rai::random_pool.GenerateBlock (wallet.bytes.begin (), wallet.bytes.size ());
			assert (!wallet.is_zero ());
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
                auto wallet_l (tree.get <std::string> ("wallet"));
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
                    error_a = error_a | wallet.decode_hex (wallet_l);
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
            std::string wallet_l;
            wallet.encode_hex (wallet_l);
            tree.put ("wallet", wallet_l);
            tree.put ("distribution_account", distribution_account.to_base58check ());
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
        rai::uint256_union wallet;
    };
    uint64_t distribution_amount (uint64_t interval)
    {
		// Halfing period ~= Exponent of 2 in secounds approixmately 1 year = 2^25 = 33554432
		// Interval = Exponent of 2 in seconds approximately 1 minute = 2^6 = 64
		uint64_t intervals_per_period (2^25 / 2^6);
        uint64_t result;
        if (interval < intervals_per_period * 1)
        {
			// Total supply / 2^halfing period / intervals per period / user scaling
			// 2^128 / 2^1 / (2^25 / 2^6) / 10^20
            result = 3245185536584; // 50%
        }
        else if (interval < intervals_per_period * 2)
        {
            result = 1622592768292; // 25%
        }
        else if (interval < intervals_per_period * 3)
        {
            result = 811296384146; // 13%
        }
        else if (interval < intervals_per_period * 4)
        {
            result = 405648192073; // 6.3%
        }
        else if (interval < intervals_per_period * 5)
        {
            result = 202824096036; // 3.1%
        }
        else if (interval < intervals_per_period * 6)
        {
            result = 101412048018; // 1.6%
        }
        else if (interval < intervals_per_period * 7)
        {
            result = 50706024009; // 0.8%
        }
        else if (interval < intervals_per_period * 8)
        {
            result = 50706024009; // 0.8*
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
    void distribute (rai::node & node_a, std::shared_ptr <rai::wallet> wallet_a, rai::landing::config & config_a, boost::filesystem::path working_path_a)
    {
        auto now (rai::landing::seconds_since_epoch ());
        auto error (false);
        while (!error && config_a.last < now)
        {
            ++config_a.last;
            auto amount (distribution_amount (config_a.last - config_a.start));
            error = wallet_a->send_all (config_a.distribution_account, rai::scale_up (amount));
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
        node_a.service.add (std::chrono::system_clock::now () + std::chrono::seconds (16), [&node_a, &config_a, working_path_a, wallet_a] () {rai::landing::distribute (node_a, wallet_a, config_a, working_path_a);});
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

int main (int argc, char * const * argv)
{
    auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
    auto config_error (false);
    rai::landing::config config (rai::landing::read_config (config_error, working));
    if (!config_error)
    {
        rai::node_init init;
        auto service (boost::make_shared <boost::asio::io_service> ());
        rai::processor_service processor;
        auto node (std::make_shared <rai::node> (init, service, config.peering_port, working, processor));
        if (!init.error ())
        {
            node->bootstrap_peers = config.bootstrap_peers;
            node->start ();
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
            auto now (rai::landing::seconds_since_epoch ());
            if (now - config.last > 0)
            {
                std::cout << boost::str (boost::format ("The last distribution was %1% minutes ago\n") % (now - config.last));
            }
            else
            {
                std::cout << boost::str (boost::format ("Distribution will begin in %1% minutes\n") % (config.last - now));
            }
            auto wallet (node->wallets.open (config.wallet));
            if (wallet == nullptr)
            {
                wallet = node->wallets.create (config.wallet);
            }
            auto wallet_entry (wallet->store.begin ());
            if (wallet_entry == wallet->store.end ())
            {
                rai::keypair key;
                wallet->store.insert (key.prv);
                wallet_entry = wallet->store.begin ();
            }
            assert (wallet_entry != wallet->store.end ());
            std::cout << boost::str (boost::format ("Landing account: %1%\n") % wallet_entry->first.to_base58check ());
            ++wallet_entry;
            assert (wallet_entry == wallet->store.end ());
            std::cout << "Type a line to start\n";
            std::string line;
            std::cin >> line;
            rai::landing::distribute (*node, wallet, config, working);
            network_thread.join ();
            processor_thread.join ();
        }
        else
        {
            std::cerr << "Error initializing node\n";
        }
    }
    else
    {
        std::cerr << "Error loading configuration\n";
    }
}