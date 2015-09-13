#include <rai/node.hpp>

#include <rai/working.hpp>

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
	landing_config (bool & error_a, std::istream & stream_a)
	{
		error_a = false;
		boost::property_tree::ptree tree;
		try
		{
			boost::property_tree::read_json (stream_a, tree);
			auto wallet_l (tree.get <std::string> ("wallet"));
			auto node_l (tree.get_child ("node"));
			try
			{
				error_a = error_a | wallet.decode_hex (wallet_l);
				error_a = error_a | node.deserialize_json (node_l);
			}
			catch (std::logic_error const &)
			{
				error_a = true;
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
	void serialize (std::ostream & stream_a) const
	{
		boost::property_tree::ptree tree;
		std::string wallet_l;
		wallet.encode_hex (wallet_l);
		tree.put ("wallet", wallet_l);
		boost::property_tree::ptree node_l;
		node.serialize_json (node_l);
		tree.add_child ("node", node_l);
		boost::property_tree::write_json (stream_a, tree);
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
	auto config_error (false);
	rai::landing_config config;
	{
		std::ifstream config_stream;
		config_stream.open ((working / "config.json").string ());
		if (!config_stream.fail ())
		{
			rai::landing_config loaded_config (config_error, config_stream);
			config = loaded_config;
		}
		else
		{
			std::ofstream config_stream;
			config_stream.open ((working / "config.json").string ());
			if (!config_stream.fail ())
			{
				config.serialize (config_stream);
			}
		}
	}
	if (!config_error)
	{
		rai::landing_store store;
		{
			std::ifstream store_stream;
			store_stream.open ((working / "landing.json").string ());
			if (!store_stream.fail ())
			{
				rai::landing_store loaded_store (config_error, store_stream);
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
		if (!config_error)
		{
			rai::node_init init;
			auto service (boost::make_shared <boost::asio::io_service> ());
			rai::work_pool work;
			rai::processor_service processor;
			auto node (std::make_shared <rai::node> (init, service, working, processor, config.node, work));
			if (!init.error ())
			{
				node->start ();
				rai::thread_runner runner (*service, processor);
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
					std::cout << boost::str (boost::format ("Landing account: %1%\n") % store.source.to_base58check ());
					std::cout << boost::str (boost::format ("Destination account: %1%\n") % store.destination.to_base58check ());
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
}