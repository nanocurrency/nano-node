#include <banano/node/node.hpp>
#include <banano/node/testing.hpp>
#include <banano/bananode/daemon.hpp>

#include <argon2.h>

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
	boost::program_options::options_description description ("Command line options");
	rai::add_node_options (description);

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
		("platform", boost::program_options::value<std::string> (), "Defines the <platform> for OpenCL commands")
		("device", boost::program_options::value<std::string> (), "Defines <device> for OpenCL command")
		("threads", boost::program_options::value<std::string> (), "Defines <threads> count for OpenCL command");
	// clang-format on

	boost::program_options::variables_map vm;
	boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	boost::program_options::notify (vm);
	int result (0);
	boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
	if (!rai::handle_node_options (vm))
	{
	}
	else if (vm.count ("daemon") > 0)
	{
		banano_daemon::daemon daemon;
		daemon.run (data_path);
	}
	else if (vm.count ("debug_block_count"))
	{
		rai::inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		std::cout << boost::str (boost::format ("Block count: %1%\n") % node.node->store.block_count (transaction).sum ());
	}
	else if (vm.count ("debug_bootstrap_generate"))
	{
		if (vm.count ("key") == 1)
		{
			rai::uint256_union key;
			if (!key.decode_hex (vm["key"].as<std::string> ()))
			{
				rai::keypair genesis (key.to_string ());
				rai::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
				std::cout << "Genesis: " << genesis.prv.data.to_string () << std::endl
				          << "Public: " << genesis.pub.to_string () << std::endl
				          << "Account: " << genesis.pub.to_account () << std::endl;
				rai::keypair landing;
				std::cout << "Landing: " << landing.prv.data.to_string () << std::endl
				          << "Public: " << landing.pub.to_string () << std::endl
				          << "Account: " << landing.pub.to_account () << std::endl;
				for (auto i (0); i != 32; ++i)
				{
					rai::keypair rep;
					std::cout << "Rep" << i << ": " << rep.prv.data.to_string () << std::endl
					          << "Public: " << rep.pub.to_string () << std::endl
					          << "Account: " << rep.pub.to_account () << std::endl;
				}
				rai::uint128_t balance (std::numeric_limits<rai::uint128_t>::max ());
				rai::open_block genesis_block (genesis.pub, genesis.pub, genesis.pub, genesis.prv, genesis.pub, work.generate (genesis.pub));
				std::cout << genesis_block.to_json ();
				rai::block_hash previous (genesis_block.hash ());
				for (auto i (0); i != 8; ++i)
				{
					rai::uint128_t yearly_distribution (rai::uint128_t (1) << (127 - (i == 7 ? 6 : i)));
					auto weekly_distribution (yearly_distribution / 52);
					for (auto j (0); j != 52; ++j)
					{
						assert (balance > weekly_distribution);
						balance = balance < (weekly_distribution * 2) ? 0 : balance - weekly_distribution;
						rai::send_block send (previous, landing.pub, balance, genesis.prv, genesis.pub, work.generate (previous));
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
		rai::inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		rai::uint128_t total;
		for (auto i (node.node->store.representation_begin (transaction)), n (node.node->store.representation_end ()); i != n; ++i)
		{
			rai::account account (i->first.uint256 ());
			auto amount (node.node->store.representation_get (transaction, account));
			total += amount;
			std::cout << boost::str (boost::format ("%1% %2% %3%\n") % account.to_account () % amount.convert_to<std::string> () % total.convert_to<std::string> ());
		}
		std::map<rai::account, rai::uint128_t> calculated;
		for (auto i (node.node->store.latest_begin (transaction)), n (node.node->store.latest_end ()); i != n; ++i)
		{
			rai::account_info info (i->second);
			rai::block_hash rep_block (node.node->ledger.representative_calculated (transaction, info.head));
			std::unique_ptr<rai::block> block (node.node->store.block_get (transaction, rep_block));
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
		rai::inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		std::cout << boost::str (boost::format ("Frontier count: %1%\n") % node.node->store.account_count (transaction));
	}
	else if (vm.count ("debug_mass_activity"))
	{
		rai::system system (24000, 1);
		size_t count (1000000);
		system.generate_mass_activity (count, *system.nodes[0]);
	}
	else if (vm.count ("debug_profile_kdf"))
	{
		rai::uint256_union result;
		rai::uint256_union salt (0);
		std::string password ("");
		for (; true;)
		{
			auto begin1 (std::chrono::high_resolution_clock::now ());
			auto success (argon2_hash (1, rai::wallet_store::kdf_work, 1, password.data (), password.size (), salt.bytes.data (), salt.bytes.size (), result.bytes.data (), result.bytes.size (), NULL, 0, Argon2_d, 0x10));
			auto end1 (std::chrono::high_resolution_clock::now ());
			std::cerr << boost::str (boost::format ("Derivation time: %1%us\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
		}
	}
	else if (vm.count ("debug_profile_generate"))
	{
		rai::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
		rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
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
		rai::opencl_environment environment (error);
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
						rai::logging logging;
						auto opencl (rai::opencl_work::create (true, { platform, device, threads }, logging));
						rai::work_pool work_pool (std::numeric_limits<unsigned>::max (), opencl ? [&opencl](rai::uint256_union const & root_a) {
							return opencl->generate_work (root_a);
						}
						                                                                        : std::function<boost::optional<uint64_t> (rai::uint256_union const &)> (nullptr));
						rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
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
		rai::work_pool work (std::numeric_limits<unsigned>::max (), nullptr);
		rai::change_block block (0, 0, rai::keypair ().prv, 0, 0);
		std::cerr << "Starting verification profiling\n";
		for (uint64_t i (0); true; ++i)
		{
			block.hashables.previous.qwords[0] += 1;
			auto begin1 (std::chrono::high_resolution_clock::now ());
			for (uint64_t t (0); t < 1000000; ++t)
			{
				block.hashables.previous.qwords[0] += 1;
				block.block_work_set (t);
				rai::work_validate (block);
			}
			auto end1 (std::chrono::high_resolution_clock::now ());
			std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
		}
	}
	else if (vm.count ("debug_verify_profile"))
	{
		rai::keypair key;
		rai::uint256_union message;
		rai::uint512_union signature;
		signature = rai::sign_message (key.prv, key.pub, message);
		auto begin (std::chrono::high_resolution_clock::now ());
		for (auto i (0u); i < 1000; ++i)
		{
			rai::validate_message (key.pub, message, signature);
		}
		auto end (std::chrono::high_resolution_clock::now ());
		std::cerr << "Signature verifications " << std::chrono::duration_cast<std::chrono::microseconds> (end - begin).count () << std::endl;
	}
	else if (vm.count ("debug_profile_sign"))
	{
		std::cerr << "Starting blocks signing profiling\n";
		for (uint64_t i (0); true; ++i)
		{
			rai::keypair key;
			rai::block_hash latest (0);
			auto begin1 (std::chrono::high_resolution_clock::now ());
			for (uint64_t balance (0); balance < 1000; ++balance)
			{
				rai::send_block send (latest, key.pub, balance, key.prv, key.pub, 0);
				latest = send.hash ();
			}
			auto end1 (std::chrono::high_resolution_clock::now ());
			std::cerr << boost::str (boost::format ("%|1$ 12d|\n") % std::chrono::duration_cast<std::chrono::microseconds> (end1 - begin1).count ());
		}
	}
	else if (vm.count ("version"))
	{
		std::cout << "Version " << BANANO_VERSION_MAJOR << "." << BANANO_VERSION_MINOR << std::endl;
	}
	else
	{
		std::cout << description << std::endl;
		result = -1;
	}
	return result;
}
