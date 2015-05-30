#include <rai/node.hpp>

#include <rai/working.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <fstream>
#include <thread>

namespace rai
{
class landing_config
{
public:
	landing_config () :
	landing_file ("landing.json"),
	peering_port (rai::network::node_port)
	{
		preconfigured_peers.push_back ("rai.raiblocks.net");
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
			auto peering_port_l (tree.get <std::string> ("peering_port"));
			auto bootstrap_peers_l (tree.get_child ("preconfigured_peers"));
			auto wallet_l (tree.get <std::string> ("wallet"));
			auto logging_l (tree.get_child ("logging"));
			preconfigured_peers.clear ();
			for (auto i (bootstrap_peers_l.begin ()), n (bootstrap_peers_l.end ()); i != n; ++i)
			{
				auto bootstrap_peer (i->second.get <std::string> (""));
				preconfigured_peers.push_back (bootstrap_peer);
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
			error_a = error_a | logging.deserialize_json (logging_l);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
	void serialize (std::ostream & stream_a) const
	{
		boost::property_tree::ptree tree;
		tree.put ("peering_port", std::to_string (peering_port));
		std::string wallet_l;
		wallet.encode_hex (wallet_l);
		tree.put ("wallet", wallet_l);
		boost::property_tree::ptree bootstrap_peers_l;
		for (auto i (preconfigured_peers.begin ()), n (preconfigured_peers.end ()); i != n; ++i)
		{
			boost::property_tree::ptree entry;
			entry.put ("", *i);
			bootstrap_peers_l.push_back (std::make_pair ("", entry));
		}
		tree.add_child ("preconfigured_peers", bootstrap_peers_l);
		boost::property_tree::ptree logging_l;
		logging.serialize_json (logging_l);
		tree.add_child ("logging", logging_l);
		boost::property_tree::write_json (stream_a, tree);
	}
	std::vector <std::string> preconfigured_peers;
	std::string landing_file;
	uint16_t peering_port;
	rai::uint256_union wallet;
	rai::logging logging;
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
			rai::processor_service processor;
			auto node (std::make_shared <rai::node> (init, service, config.peering_port, working, processor, config.logging));
			if (!init.error ())
			{
				node->preconfigured_peers = config.preconfigured_peers;
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
}