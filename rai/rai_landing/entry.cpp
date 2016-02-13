#include <rai/node/testing.hpp>

#include <rai/node/working.hpp>

#include <boost/make_shared.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <fstream>

namespace rai
{
class landing_config
{
public:
	landing_config () :
	landing_file ("landing.json")
	{
		rai::random_pool.GenerateBlock (wallet.bytes.begin (), wallet.bytes.size ());
		assert (!wallet.is_zero ());
	}
	bool deserialize_json (bool & upgraded_a, boost::property_tree::ptree & tree_a)
	{
		auto error (false);
		try
		{
			if (!tree_a.empty ())
			{
				auto wallet_l (tree_a.get <std::string> ("wallet"));
				auto & node_l (tree_a.get_child ("node"));
				try
				{
					error |= wallet.decode_hex (wallet_l);
					error |= node.deserialize_json (upgraded_a, node_l);
				}
				catch (std::logic_error const &)
				{
					error = true;
				}
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
	void serialize_json (boost::property_tree::ptree & tree_a) const
	{
		std::string wallet_l;
		wallet.encode_hex (wallet_l);
		tree_a.put ("wallet", wallet_l);
		boost::property_tree::ptree node_l;
		node.serialize_json (node_l);
		tree_a.add_child ("node", node_l);
	}
	std::string landing_file;
	rai::uint256_union wallet;
	rai::node_config node;
};
}

int main (int argc, char * const * argv)
{
    auto working (rai::working_path ());
	boost::filesystem::create_directories (working);
	rai::landing_config config;
	auto config_path ((working / "config.json").string ());
	std::fstream config_file;
	rai::open_or_create (config_file, config_path);
    if (!config_file.fail ())
	{
		auto error (rai::fetch_object (config, config_file));
		if (!error)
		{
			rai::landing_store store;
			{
				std::ifstream store_stream;
				store_stream.open ((working / "landing.json").string ());
				if (!store_stream.fail ())
				{
					rai::landing_store loaded_store (error, store_stream);
					store = loaded_store;
				}
				else
				{
					std::ofstream store_stream;
					store_stream.open ((working / "landing.json").string ());
					if (!store_stream.fail ())
					{
						store.serialize (store_stream);
					}
				}
			}
			if (!error)
			{
				rai::node_init init;
				auto service (boost::make_shared <boost::asio::io_service> ());
				rai::work_pool work;
				rai::alarm alarm (*service);
				auto node (std::make_shared <rai::node> (init, *service, working, alarm, config.node, work));
				if (!init.error ())
				{
					node->start ();
					rai::thread_runner runner (*service, node->config.io_threads);
					auto wallet (node->wallets.open (config.wallet));
					if (wallet == nullptr)
					{
						wallet = node->wallets.create (config.wallet);
					}
					rai::landing landing (*node, wallet, store, working / "landing.json");
					auto now (landing.seconds_since_epoch ());
					std::cout << boost::str (boost::format ("Current time: %1%\n") % now);
					if (now - store.last > 0)
					{
						std::cout << boost::str (boost::format ("The last distribution was %1% seconds ago\n") % (now - store.last));
					}
					else
					{
						std::cout << boost::str (boost::format ("Distribution will begin in %1% seconds\n") % (store.last - now));
					}
					{
						rai::transaction transaction (node->store.environment, nullptr, true);
						auto wallet_entry (wallet->store.begin (transaction));
						if (wallet_entry == wallet->store.end ())
						{
							rai::keypair key;
							wallet->store.insert (transaction, key.prv);
							wallet_entry = wallet->store.begin (transaction);
							store.destination = key.pub;
							store.source = key.pub;
							store.start = now;
							store.last = now;
							landing.write_store ();
						}
						assert (wallet_entry != wallet->store.end ());
						std::cout << boost::str (boost::format ("Landing account: %1%\n") % store.source.to_account ());
						std::cout << boost::str (boost::format ("Destination account: %1%\n") % store.destination.to_account ());
						++wallet_entry;
						assert (wallet_entry == wallet->store.end ());
					}
					std::cout << "Type a line to start\n";
					std::string line;
					std::cin >> line;
					landing.distribute_ongoing ();
					runner.join ();
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
		else
		{
			std::cerr << "Error deserializing config file\n";
		}
	}
}