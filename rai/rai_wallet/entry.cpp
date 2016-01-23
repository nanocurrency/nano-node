#include <rai/qt/qt.hpp>

#include <rai/node/working.hpp>
#include <rai/icon.hpp>

#include <boost/make_shared.hpp>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

class qt_wallet_config
{
public:
	qt_wallet_config (rai::account & account_a) :
	account (account_a)
	{
		rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
		assert (!wallet.is_zero ());
	}
	bool deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
	{
		auto error (false);
		if (!tree_a.empty ())
		{
			auto wallet_l (tree_a.get <std::string> ("wallet"));
			auto account_l (tree_a.get <std::string> ("account"));
			auto node_l (tree_a.get_child ("node"));
			try
			{
				error |= wallet.decode_hex (wallet_l);
				error |= account.decode_base58check (account_l);
				error |= node.deserialize_json (upgraded_a, node_l);
				if (wallet.is_zero ())
				{
					rai::random_pool.GenerateBlock (wallet.bytes.data (), wallet.bytes.size ());
					upgraded_a = true;
				}
			}
			catch (std::logic_error const &)
			{
				error = true;
			}
		}
		else
		{
			serialize_json (tree_a);
			upgraded_a = true;
		}
		return error;
	}
	void serialize_json (boost::property_tree::ptree & tree_a)
	{
		std::string wallet_string;
		wallet.encode_hex (wallet_string);
		tree_a.put ("wallet", wallet_string);
		tree_a.put ("account", account.to_base58check ());
		boost::property_tree::ptree node_l;
		node.serialize_json (node_l);
		tree_a.add_child ("node", node_l);
	}
	rai::uint256_union wallet;
	rai::account account;
	rai::node_config node;
};

int run_wallet (int argc, char * const * argv)
{
	auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
    rai::keypair key;
	qt_wallet_config config (key.pub);
	auto config_path ((working / "config.json").string ());
	std::fstream config_file;
	rai::open_or_create (config_file, config_path);
    int result (0);
    if (!config_file.fail ())
    {
		auto error (rai::fetch_object (config, config_file));
		if (!error)
		{
			QApplication application (argc, const_cast <char **> (argv));
			rai::set_application_icon (application);
			auto service (boost::make_shared <boost::asio::io_service> ());
			rai::work_pool work;
			rai::processor_service processor;
			rai::node_init init;
			auto node (std::make_shared <rai::node> (init, *service, working, processor, config.node, work));
			if (!init.error ())
			{
				if (config.account == key.pub)
				{
					auto wallet (node->wallets.create (config.wallet));
					wallet->insert (key.prv);
					assert (wallet->exists (config.account));
				}
				key.prv.data.clear ();
				auto wallet (node->wallets.open (config.wallet));
				if (wallet != nullptr)
				{
					if (wallet->exists (config.account))
					{
						QObject::connect (&application, &QApplication::aboutToQuit, [&] ()
						{
							node->stop ();
						});
						node->start ();
						std::unique_ptr <rai_qt::wallet> gui (new rai_qt::wallet (application, *node, wallet, config.account));
						gui->client_window->show ();
						rai::thread_runner runner (*service, processor);
						try
						{
							result = application.exec ();
						}
						catch (...)
						{
							result = -1;
							assert (false);
						}
						runner.join ();
					}
					else
					{
						std::cerr << "Wallet account doesn't exist\n";
					}
				}
				else
				{
					std::cerr << "Wallet id doesn't exist\n";
				}
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
        std::cerr << "Unable to open config file\n";
    }
	return result;
}

int main (int argc, char * const * argv)
{
	boost::program_options::options_description description ("Command line options");
	description.add_options () ("help", "Print out options");
	rai::add_node_options (description);
	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
	boost::program_options::notify (vm);
	int result (0);
	if (!rai::handle_node_options (vm))
	{
	}
	else if (vm.count ("help") != 0)
	{
		std::cout << description << std::endl;
	}
    else
    {
		result = run_wallet (argc, argv);
    }
    return result;
}
