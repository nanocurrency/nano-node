#include <rai/qt/qt.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thread>

class qt_wallet_config
{
public:
    qt_wallet_config () :
    peering_port (rai::network::node_port),
    wallet (0),
    account (0)
    {
        bootstrap_peers.push_back ("rai.raiblocks.net");
    }
    qt_wallet_config (bool & error_a, std::istream & stream_a)
    {
        error_a = false;
        boost::property_tree::ptree tree;
        try
        {
            boost::property_tree::read_json (stream_a, tree);
            auto peering_port_l (tree.get <std::string> ("peering_port"));
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
                peering_port = std::stoul (peering_port_l);
                error_a = peering_port > std::numeric_limits <uint16_t>::max ();
                error_a = error_a | wallet.decode_hex (wallet_l);
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
    void serialize (std::ostream & stream_a)
    {
        boost::property_tree::ptree tree;
        tree.put ("peering_port", std::to_string (peering_port));
        std::string wallet_string;
        wallet.encode_hex (wallet_string);
        tree.put ("wallet", wallet_string);
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
    bool uninitialized ()
    {
        auto result (wallet.is_zero ());
        assert (result == account.is_zero ());
        return result;
    }
    std::vector <std::string> bootstrap_peers;
    uint16_t peering_port;
    rai::uint256_union wallet;
    rai::account account;
};

int main (int argc, char * const * argv)
{
    auto working (boost::filesystem::system_complete (argv[0]).parent_path ());
    auto config_error (false);
    qt_wallet_config config;
    auto config_path ((working / "config.json").string ());
    std::ifstream config_file;
    config_file.open (config_path);
    if (!config_file.fail ())
    {
        config = qt_wallet_config (config_error, config_file);
    }
    if (!config_error)
    {
        QApplication application (argc, const_cast <char **> (argv));
        auto service (boost::make_shared <boost::asio::io_service> ());
        rai::processor_service processor;
        rai::node_init init;
        auto node (std::make_shared <rai::node> (init, service, config.peering_port, working, processor));
        if (!init.error ())
        {
            if (config.uninitialized ())
            {
                rai::random_pool.GenerateBlock (config.wallet.bytes.data (), config.wallet.bytes.size ());
                auto wallet (node->wallets.create (config.wallet));
                rai::keypair key;
                config.account = key.pub;
                wallet->store.insert (key.prv);
                std::ofstream config_file;
                config_file.open (config_path);
                if (!config_file.fail ())
                {
                    config.serialize (config_file);
                }
            }
            auto wallet (node->wallets.open (config.wallet));
            if (wallet != nullptr)
            {
                if (wallet->store.exists (config.account))
                {
                    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
                    {
                        node->stop ();
                    });
                    node->bootstrap_peers = config.bootstrap_peers;
                    node->start ();
                    std::unique_ptr <rai_qt::wallet> gui (new rai_qt::wallet (application, *node, wallet));
                    gui->client_window->show ();
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
                    int result;
                    try
                    {
                        result = application.exec ();
                    }
                    catch (...)
                    {
                        result = -1;
                        assert (false);
                    }
                    network_thread.join ();
                    processor_thread.join ();
                    return result;
                }
                else
                {
                    std::cerr << "Wallet account doesn't exist";
                }
            }
            else
            {
                std::cerr << "Wallet id doesn't exist";
            }
        }
        else
        {
            std::cerr << "Error initializing node\n";
        }
    }
}