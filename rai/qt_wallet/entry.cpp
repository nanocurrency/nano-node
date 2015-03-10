#include <rai/qt/qt.hpp>

#include <rai/working.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thread>

class qt_wallet_config
{
public:
    qt_wallet_config () :
    peering_port (rai::network::node_port),
    account (0)
    {
		rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
        preconfigured_peers.push_back ("rai.raiblocks.net");
		assert (!wallet.is_zero ());
    }
    qt_wallet_config (bool & error_a, std::istream & stream_a)
    {
        error_a = false;
        boost::property_tree::ptree tree;
        try
        {
            boost::property_tree::read_json (stream_a, tree);
            auto peering_port_l (tree.get <std::string> ("peering_port"));
            auto preconfigured_peers_l (tree.get_child ("bootstrap_peers"));
            auto wallet_l (tree.get <std::string> ("wallet"));
            auto account_l (tree.get <std::string> ("account"));
			auto logging_l (tree.get_child ("logging"));
            preconfigured_peers.clear ();
            for (auto i (preconfigured_peers_l.begin ()), n (preconfigured_peers_l.end ()); i != n; ++i)
            {
                auto bootstrap_peer (i->second.get <std::string> (""));
                preconfigured_peers.push_back (bootstrap_peer);
            }
            try
            {
                peering_port = std::stoul (peering_port_l);
                error_a = peering_port > std::numeric_limits <uint16_t>::max ();
                error_a = error_a | wallet.decode_hex (wallet_l);
                error_a = error_a | account.decode_base58check (account_l);
				error_a = error_a | logging.deserialize_json (logging_l);
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
        tree.put ("account", account.to_base58check ());
        boost::property_tree::ptree bootstrap_peers_l;
        for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
        {
            boost::property_tree::ptree entry;
            entry.put ("", *i);
            bootstrap_peers_l.push_back (std::make_pair ("", entry));
        }
        tree.add_child ("bootstrap_peers", bootstrap_peers_l);
		boost::property_tree::ptree logging_l;
		logging.serialize_json (logging_l);
		tree.add_child ("logging", logging_l);
        boost::property_tree::write_json (stream_a, tree);
    }
    std::vector <std::string> preconfigured_peers;
    uint16_t peering_port;
    rai::uint256_union wallet;
    rai::account account;
	rai::logging logging;
};

int main (int argc, char * const * argv)
{
    auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
    auto config_error (false);
    qt_wallet_config config;
    auto config_path ((working / "config.json").string ());
    std::ifstream config_file;
    config_file.open (config_path);
	auto uninitialized (true);
    if (!config_file.fail ())
    {
        config = qt_wallet_config (config_error, config_file);
		uninitialized = false;
    }
    if (!config_error)
    {
        QApplication application (argc, const_cast <char **> (argv));
        auto service (boost::make_shared <boost::asio::io_service> ());
        rai::processor_service processor;
        rai::node_init init;
        auto node (std::make_shared <rai::node> (init, service, config.peering_port, working, processor, config.logging));
        if (!init.error ())
        {
			rai::transaction transaction (node->store.environment, nullptr, false);
            if (uninitialized)
            {
                auto wallet (node->wallets.create (config.wallet));
                rai::keypair key;
                config.account = key.pub;
                wallet->store.insert (transaction, key.prv);
				assert (wallet->store.exists (transaction, config.account));
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
                if (wallet->store.exists (transaction, config.account))
                {
                    QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
                    {
                        node->stop ();
                    });
                    node->preconfigured_peers = config.preconfigured_peers;
                    node->start ();
                    std::unique_ptr <rai_qt::wallet> gui (new rai_qt::wallet (application, *node, wallet, config.account));
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
    else
    {
        std::cerr << "Error in config file";
    }
}