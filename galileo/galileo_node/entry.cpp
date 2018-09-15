#include <galileo/node/cli.hpp>
#include <galileo/node/node.hpp>
#include <galileo/node/testing.hpp>
#include <galileo/galileo_node/daemon.hpp>

#include <argon2.h>

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
	boost::program_options::options_description description ("Command line options");
	galileo::add_node_options (description);

	// clang-format off
	description.add_options ()
		("help", "Print out options")
		("version", "Prints out version")
		("daemon", "Start node daemon")
		("debug_block_count", "Display the number of block")
		("debug_bootstrap_generate", "Generate bootstrap sequence of blocks")
		("debug_dump_representatives", "List representatives and weights")
		("debug_account_count", "Display the number of accounts")
		("debug_mass_activity", "Generates fake debug activity")
		("debug_profile_generate", "Profile work generation")
		("debug_opencl", "OpenCL work generation")
		("debug_profile_verify", "Profile work verification")
		("debug_profile_kdf", "Profile kdf function")
		("debug_verify_profile", "Profile signature verification")
		("debug_profile_sign", "Profile signature generation")
		("debug_profile_process", "Profile active blocks processing (only for galileo_test_network)")
		("debug_validate_blocks", "Check all blocks for correct hash, signature, work value")
		("platform", boost::program_options::value<std::string> (), "Defines the <platform> for OpenCL commands")
		("device", boost::program_options::value<std::string> (), "Defines <device> for OpenCL command")
		("threads", boost::program_options::value<std::string> (), "Defines <threads> count for OpenCL command");
	// clang-format on

	boost::program_options::variables_map vm;
	try
	{
		boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	}
	catch (boost::program_options::error const & err)
	{
		std::cerr << err.what () << std::endl;
		return 1;
	}
	boost::program_options::notify (vm);
	int result (0);
	boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : galileo::working_path ();
	auto ec = galileo::handle_node_options (vm);

	if (ec == galileo::error_cli::unknown_command)
	{
		if (vm.count ("daemon") > 0)
		{
			galileo_daemon::daemon daemon;
			daemon.run (data_path);
		}
		else if (vm.count ("debug_block_count"))
		{
			galileo::inactive_node node (data_path);
			auto transaction (node.node->store.tx_begin ());
			std::cout << boost::str (boost::format ("Block count: %1%\n") % node.node->store.block_count (transaction).sum ());
		}
		else if (vm.count ("debug_bootstrap_generate"))
		{
			if (vm.count ("key") == 1)
			{
				galileo::uint256_union key;
				if (!key.decode_hex (vm["key"].as<std::string> ()))
				{
					galileo::keypair genesis (key.to_string ());
					galileo::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
					std::cout << "Genesis: " << genesis.prv.data.to_string () << std::endl
					          << "Public: " << genesis.pub.to_string () << std::endl
					          << "Account: " << genesis.pub.to_account () << std::endl;
					galileo::keypair landing;
					std::cout << "Landing: " << landing.prv.data.to_string () << std::endl
					          << "Public: " << landing.pub.to_string () << std::endl
					          << "Account: " << landing.pub.to_account () << std::endl;
					for (auto i (0); i != 32; ++i)
					{
						galileo::keypair rep;
						std::cout << "Rep" << i << ": " << rep.prv.data.to_string () << std::endl
						          << "Public: " << rep.pub.to_string () << std::endl
						          << "Account: " << rep.pub.to_account () << std::endl;
					}
					galileo::uint128_t balance (std::numeric_limits<galileo::uint128_t>::max ());
					galileo::open_block genesis_block (genesis.pub, genesis.pub, genesis.pub, genesis.prv, genesis.pub, work.generate (genesis.pub));
					std::cout << genesis_block.to_json ();
					galileo::block_hash previous (genesis_block.hash ());
					for (auto i (0); i != 8; ++i)
					{
						galileo::uint128_t yearly_distribution (galileo::uint128_t (1) << (127 - (i == 7 ? 6 : i)));
						auto weekly_distribution (yearly_distribution / 52);
						for (auto j (0); j != 52; ++j)
						{
							assert (balance > weekly_distribution);
							balance = balance < (weekly_distribution * 2) ? 0 : balance - weekly_distribution;
							galileo::send_block send (previous, landing.pub, balance, genesis.prv, genesis.pub, work.generate (previous));
							previous = send.hash ();
							std::cout << send.to_json ();
							std::cout.flush ();
						}
					}
				}
				else
				{
					std::cerr << "Invalid key\n";
					result = -1;
				}
			}
			else
			{
				std::cerr << "Bootstrapping requires one <key> option\n";
				result = -1;
			}
		}
		else if (vm.count ("debug_dump_representatives"))
		{
			galileo::inactive_node node (data_path);
			auto transaction (node.node->store.tx_begin ());
			galileo::uint128_t total;
			for (auto i (node.node->store.representation_begin (transaction)), n (node.node->store.representation_end ()); i != n; ++i)
			{
				galileo::account account (i->first);
				auto amount (node.node->store.representation_get (transaction, account));
				total += amount;
				std::cout << boost::str (boost::format ("%1% %2% %3%\n") % account.to_account () % amount.convert_to<std::string> () % total.convert_to<std::string> ());
			}
			std::map<galileo::account, galileo::uint128_t> calculated;
			for (auto i (node.node->store.latest_begin (transaction)), n (node.node->store.latest_end ()); i != n; ++i)
			{
				galileo::account_info info (i->second);
				galileo::block_hash rep_block (node.node->ledger.representative_calculated (transaction, info.head));
				std::unique_ptr<galileo::block> block (node.node->store.block_get (transaction, rep_block));
				calculated[block->representative ()] += info.balance.number ();
			}
			total = 0;
			for (auto i (calculated.begin ()), n (calculated.end ()); i != n; ++i)
			{
				total += i->second;
				std::cout << boost::str (boost::format ("%1% %2% %3%\n") % i->first.to_account () % i->second.convert_to<std::string> () % total.convert_to<std::string> ());
			}
		}
		else if (vm.count ("debug_account_count"))
		{
			galileo::inactive_node node (data_path);
			auto transaction (node.node->store.tx_begin ());
			std::cout << boost::str (boost::format ("Frontier count: %1%\n") % node.node->store.account_count (transaction));
		}
		else if (vm.count ("debug_mass_activity"))
		{
			galileo::system system (24000, 1);
			size_t count (1000000);
			system.generate_mass_activity (count, *system.nodes[0]);
		}
		else if (vm.count ("debug_profile_kdf"))
		{
			galileo::uint256_union result;
			galileo::uint256_union salt (0);
			std::string password ("");
			for (; true;)
			{
				auto begin1 (std::chrono::high_resolution_clock::now ());
				auto success (argon2_hash (1, galileo::wallet_store::kdf_work, 1, password.data (), password.size (), salt.bytes.data (), salt.bytes.size (), result.bytes.data (), result.bytes.size (), NULL, 0, Argon2_d, 0x10));
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_profile_generate"))
		{
			galileo::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
			galileo::change_block block (0, 0, galileo::keypair ().prv, 0, 0);
			std::cerr << "Starting generation profiling\n";
			for (uint64_t i (0); true; ++i)
			{
				block.hashables.previous.qwords[0] += 1;
				auto begin1 (std::chrono::high_resolution_clock::now ());
				block.block_work_set (work.generate (block.root ()));
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_opencl"))
		{
			bool error (false);
			galileo::opencl_environment environment (error);
			if (!error)
			{
				unsigned short platform (0);
				if (vm.count ("platform") == 1)
				{
					try
					{
						platform = boost::lexical_cast<unsigned short> (vm["platform"].as<std::string> ());
					}
					catch (boost::bad_lexical_cast & e)
					{
						std::cerr << "Invalid platform id\n";
						result = -1;
					}
				}
				unsigned short device (0);
				if (vm.count ("device") == 1)
				{
					try
					{
						device = boost::lexical_cast<unsigned short> (vm["device"].as<std::string> ());
					}
					catch (boost::bad_lexical_cast & e)
					{
						std::cerr << "Invalid device id\n";
						result = -1;
					}
				}
				unsigned threads (1024 * 1024);
				if (vm.count ("threads") == 1)
				{
					try
					{
						threads = boost::lexical_cast<unsigned> (vm["threads"].as<std::string> ());
					}
					catch (boost::bad_lexical_cast & e)
					{
						std::cerr << "Invalid threads count\n";
						result = -1;
					}
				}
				if (!result)
				{
					error |= platform >= environment.platforms.size ();
					if (!error)
					{
						error |= device >= environment.platforms[platform].devices.size ();
						if (!error)
						{
							galileo::logging logging;
							auto opencl (galileo::opencl_work::create (true, { platform, device, threads }, logging));
							galileo::work_pool work_pool (std::numeric_limits<unsigned>::max (), opencl ? [&opencl](galileo::uint256_union const & root_a) {
								return opencl->generate_work (root_a);
							}
							                                                                        : std::function<boost::optional<uint64_t> (galileo::uint256_union const &)> (nullptr));
							galileo::change_block block (0, 0, galileo::keypair ().prv, 0, 0);
							std::cerr << boost::str (boost::format ("Starting OpenCL generation profiling. Platform: %1%. Device: %2%. Threads: %3%\n") % platform % device % threads);
							for (uint64_t i (0); true; ++i)
							{
								block.hashables.previous.qwords[0] += 1;
								auto begin1 (std::chrono::high_resolution_clock::now ());
								block.block_work_set (work_pool.generate (block.root ()));
								auto end1 (std::chrono::high_resolution_clock::now ());
								std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
							}
						}
						else
						{
							std::cout << "Not available device id\n"
							          << std::endl;
							result = -1;
						}
					}
					else
					{
						std::cout << "Not available platform id\n"
						          << std::endl;
						result = -1;
					}
				}
			}
			else
			{
				std::cout << "Error initializing OpenCL" << std::endl;
				result = -1;
			}
		}
		else if (vm.count ("debug_profile_verify"))
		{
			galileo::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
			galileo::change_block block (0, 0, galileo::keypair ().prv, 0, 0);
			std::cerr << "Starting verification profiling\n";
			for (uint64_t i (0); true; ++i)
			{
				block.hashables.previous.qwords[0] += 1;
				auto begin1 (std::chrono::high_resolution_clock::now ());
				for (uint64_t t (0); t < 1000000; ++t)
				{
					block.hashables.previous.qwords[0] += 1;
					block.block_work_set (t);
					galileo::work_validate (block);
				}
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_verify_profile"))
		{
			galileo::keypair key;
			galileo::uint256_union message;
			galileo::uint512_union signature;
			signature = galileo::sign_message (key.prv, key.pub, message);
			auto begin (std::chrono::high_resolution_clock::now ());
			for (auto i (0u); i < 1000; ++i)
			{
				galileo::validate_message (key.pub, message, signature);
			}
			auto end (std::chrono::high_resolution_clock::now ());
			std::cerr << "Signature verifications " << std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count () << std::endl;
		}
		else if (vm.count ("debug_profile_sign"))
		{
			std::cerr << "Starting blocks signing profiling\n";
			for (uint64_t i (0); true; ++i)
			{
				galileo::keypair key;
				galileo::block_hash latest (0);
				auto begin1 (std::chrono::high_resolution_clock::now ());
				for (uint64_t balance (0); balance < 1000; ++balance)
				{
					galileo::send_block send (latest, key.pub, balance, key.prv, key.pub, 0);
					latest = send.hash ();
				}
				auto end1 (std::chrono::high_resolution_clock::now ());
				std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
			}
		}
		else if (vm.count ("debug_profile_process"))
		{
			if (galileo::galileo_network == galileo::galileo_networks::galileo_test_network)
			{
				size_t num_accounts (100000);
				size_t num_interations (5); // 100,000 * 5 * 2 = 1,000,000 blocks
				size_t max_blocks (2 * num_accounts * num_interations + num_accounts * 2); //  1,000,000 + 2* 100,000 = 1,200,000 blocks
				std::cerr << boost::str (boost::format ("Starting pregenerating %1% blocks\n") % max_blocks);
				galileo::system system (24000, 1);
				galileo::node_init init;
				galileo::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
				galileo::logging logging;
				auto path (galileo::unique_path ());
				logging.init (path);
				auto node (std::make_shared<galileo::node> (init, system.service, 24001, path, system.alarm, logging, work));
				galileo::block_hash genesis_latest (node->latest (galileo::test_genesis_key.pub));
				galileo::uint128_t genesis_balance (std::numeric_limits<galileo::uint128_t>::max ());
				// Generating keys
				std::vector<galileo::keypair> keys (num_accounts);
				std::vector<galileo::block_hash> frontiers (num_accounts);
				std::vector<galileo::uint128_t> balances (num_accounts, 1000000000);
				// Generating blocks
				std::deque<std::shared_ptr<galileo::block>> blocks;
				for (auto i (0); i != num_accounts; ++i)
				{
					genesis_balance = genesis_balance - 1000000000;
					auto send (std::make_shared<galileo::state_block> (galileo::test_genesis_key.pub, genesis_latest, galileo::test_genesis_key.pub, genesis_balance, keys[i].pub, galileo::test_genesis_key.prv, galileo::test_genesis_key.pub, work.generate (genesis_latest)));
					genesis_latest = send->hash ();
					blocks.push_back (std::move (send));
					auto open (std::make_shared<galileo::state_block> (keys[i].pub, 0, keys[i].pub, balances[i], genesis_latest, keys[i].prv, keys[i].pub, work.generate (keys[i].pub)));
					frontiers[i] = open->hash ();
					blocks.push_back (std::move (open));
				}
				for (auto i (0); i != num_interations; ++i)
				{
					for (auto j (0); j != num_accounts; ++j)
					{
						size_t other (num_accounts - j - 1);
						// Sending to other account
						--balances[j];
						auto send (std::make_shared<galileo::state_block> (keys[j].pub, frontiers[j], keys[j].pub, balances[j], keys[other].pub, keys[j].prv, keys[j].pub, work.generate (frontiers[j])));
						frontiers[j] = send->hash ();
						blocks.push_back (std::move (send));
						// Receiving
						++balances[other];
						auto receive (std::make_shared<galileo::state_block> (keys[other].pub, frontiers[other], keys[other].pub, balances[other], frontiers[j], keys[other].prv, keys[other].pub, work.generate (frontiers[other])));
						frontiers[other] = receive->hash ();
						blocks.push_back (std::move (receive));
					}
				}
				// Processing blocks
				std::cerr << boost::str (boost::format ("Starting processing %1% active blocks\n") % max_blocks);
				auto begin (std::chrono::high_resolution_clock::now ());
				while (!blocks.empty ())
				{
					auto block (blocks.front ());
					node->process_active (block);
					blocks.pop_front ();
				}
				uint64_t block_count (0);
				while (block_count < max_blocks + 1)
				{
					std::this_thread::sleep_for (std::chrono::milliseconds (100));
					auto transaction (node->store.tx_begin ());
					block_count = node->store.block_count (transaction).sum ();
				}
				auto end (std::chrono::high_resolution_clock::now ());
				auto time (std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count ());
				node->stop ();
				std::cerr << boost::str (boost::format ("%|1$ 12d| us \n%2% blocks per second\n") % time % (max_blocks * 1000000 / time));
			}
			else
			{
				std::cerr << "For this test ACTIVE_NETWORK should be galileo_test_network" << std::endl;
			}
		}
		else if (vm.count ("debug_validate_blocks"))
		{
			galileo::inactive_node node (data_path);
			auto transaction (node.node->store.tx_begin ());
			std::cerr << boost::str (boost::format ("Performing blocks hash, signature, work validation...\n"));
			size_t count (0);
			for (auto i (node.node->store.latest_begin (transaction)), n (node.node->store.latest_end ()); i != n; ++i)
			{
				++count;
				if ((count % 20000) == 0)
				{
					std::cerr << boost::str (boost::format ("%1% accounts validated\n") % count);
				}
				galileo::account_info info (i->second);
				galileo::account account (i->first);
				auto hash (info.open_block);
				galileo::block_hash calculated_hash (0);
				while (!hash.is_zero ())
				{
					// Retrieving block data
					auto block (node.node->store.block_get (transaction, hash));
					// Check for state & open blocks if account field is correct
					if ((block->type () == galileo::block_type::open && block->root () != account) || (block->type () == galileo::block_type::state && static_cast<galileo::state_block const &> (*block.get ()).hashables.account != account))
					{
						std::cerr << boost::str (boost::format ("Incorrect account field for block %1%\n") % hash.to_string ());
					}
					// Check if previous field is correct
					if (calculated_hash != block->previous ())
					{
						std::cerr << boost::str (boost::format ("Incorrect previous field for block %1%\n") % hash.to_string ());
					}
					// Check if block data is correct (calculating hash)
					calculated_hash = block->hash ();
					if (calculated_hash != hash)
					{
						std::cerr << boost::str (boost::format ("Invalid data inside block %1% calculated hash: %2%\n") % hash.to_string () % calculated_hash.to_string ());
					}
					// Check if block signature is correct
					if (validate_message (account, hash, block->block_signature ()))
					{
						bool invalid (true);
						// Epoch blocks
						if (!node.node->ledger.epoch_link.is_zero () && block->type () == galileo::block_type::state)
						{
							auto & state_block (static_cast<galileo::state_block &> (*block.get ()));
							galileo::amount prev_balance (0);
							if (!state_block.hashables.previous.is_zero ())
							{
								prev_balance = node.node->ledger.balance (transaction, state_block.hashables.previous);
							}
							if (state_block.hashables.link == node.node->ledger.epoch_link && state_block.hashables.balance == prev_balance)
							{
								invalid = validate_message (node.node->ledger.epoch_signer, hash, block->block_signature ());
							}
						}
						if (invalid)
						{
							std::cerr << boost::str (boost::format ("Invalid signature for block %1%\n") % hash.to_string ());
						}
					}
					// Check if block work value is correct
					if (galileo::work_validate (*block.get ()))
					{
						std::cerr << boost::str (boost::format ("Invalid work for block %1% value: %2%\n") % hash.to_string () % galileo::to_string_hex (block->block_work ()));
					}
					// Retrieving successor block hash
					hash = node.node->store.block_successor (transaction, hash);
				}
			}
		}
		else if (vm.count ("version"))
		{
			std::cout << "Version " << RAIBLOCKS_VERSION_MAJOR << "." << RAIBLOCKS_VERSION_MINOR << std::endl;
		}
		else
		{
			std::cout << description << std::endl;
			result = -1;
		}
	}
	return result;
}
