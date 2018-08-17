#include <rai/lib/interface.h>
#include <rai/node/cli.hpp>
#include <rai/node/common.hpp>
#include <rai/node/node.hpp>

std::string rai::error_cli_messages::message (int ev) const
{
	switch (static_cast<rai::error_cli> (ev))
	{
		case rai::error_cli::generic:
			return "Unknown error";
		case rai::error_cli::parse_error:
			return "Coud not parse command line";
		case rai::error_cli::invalid_arguments:
			return "Invalid arguments";
		case rai::error_cli::unknown_command:
			return "Unknown command";
	}

	return "Invalid error code";
}

void rai::add_node_options (boost::program_options::options_description & description_a)
{
	// clang-format off
	description_a.add_options ()
	("account_create", "Insert next deterministic key in to <wallet>")
	("account_get", "Get account number for the <key>")
	("account_key", "Get the public key for <account>")
	("vacuum", "Compact database. If data_path is missing, the database in data directory is compacted.")
	("snapshot", "Compact database and create snapshot, functions similar to vacuum but does not replace the existing database")
	("unchecked_clear", "Clear unchecked blocks")
	("data_path", boost::program_options::value<std::string> (), "Use the supplied path as the data directory")
	("delete_node_id", "Delete the node ID in the database")
	("diagnostics", "Run internal diagnostics")
	("key_create", "Generates a adhoc random keypair and prints it to stdout")
	("key_expand", "Derive public key and account number from <key>")
	("wallet_add_adhoc", "Insert <key> in to <wallet>")
	("wallet_create", "Creates a new wallet and prints the ID")
	("wallet_change_seed", "Changes seed for <wallet> to <key>")
	("wallet_decrypt_unsafe", "Decrypts <wallet> using <password>, !!THIS WILL PRINT YOUR PRIVATE KEY TO STDOUT!!")
	("wallet_destroy", "Destroys <wallet> and all keys it contains")
	("wallet_import", "Imports keys in <file> using <password> in to <wallet>")
	("wallet_list", "Dumps wallet IDs and public keys")
	("wallet_remove", "Remove <account> from <wallet>")
	("wallet_representative_get", "Prints default representative for <wallet>")
	("wallet_representative_set", "Set <account> as default representative for <wallet>")
	("vote_dump", "Dump most recent votes from representatives")
	("account", boost::program_options::value<std::string> (), "Defines <account> for other commands")
	("file", boost::program_options::value<std::string> (), "Defines <file> for other commands")
	("key", boost::program_options::value<std::string> (), "Defines the <key> for other commands, hex")
	("password", boost::program_options::value<std::string> (), "Defines <password> for other commands")
	("wallet", boost::program_options::value<std::string> (), "Defines <wallet> for other commands");
	// clang-format on
}

std::error_code rai::handle_node_options (boost::program_options::variables_map & vm)
{
	std::error_code ec;

	boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
	if (vm.count ("account_create"))
	{
		if (vm.count ("wallet") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					if (!wallet->enter_password (password))
					{
						rai::transaction transaction (wallet->store.environment, nullptr, true);
						auto pub (wallet->store.deterministic_insert (transaction));
						std::cout << boost::str (boost::format ("Account: %1%\n") % pub.to_account ());
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("account_get") > 0)
	{
		if (vm.count ("key") == 1)
		{
			rai::uint256_union pub;
			pub.decode_hex (vm["key"].as<std::string> ());
			std::cout << "Account: " << pub.to_account () << std::endl;
		}
		else
		{
			std::cerr << "account comand requires one <key> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("account_key") > 0)
	{
		if (vm.count ("account") == 1)
		{
			rai::uint256_union account;
			account.decode_account (vm["account"].as<std::string> ());
			std::cout << "Hex: " << account.to_string () << std::endl;
		}
		else
		{
			std::cerr << "account_key command requires one <account> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("vacuum") > 0)
	{
		try
		{
			auto vacuum_path = data_path / "vacuumed.ldb";
			auto source_path = data_path / "data.ldb";
			auto backup_path = data_path / "backup.vacuum.ldb";

			std::cout << "Vacuuming database copy in " << data_path << std::endl;
			std::cout << "This may take a while..." << std::endl;

			// Scope the node so the mdb environment gets cleaned up properly before
			// the original file is replaced with the vacuumed file.
			bool success = false;
			{
				inactive_node node (data_path);
				if (vm.count ("unchecked_clear"))
				{
					rai::transaction transaction (node.node->store.environment, nullptr, true);
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("delete_node_id"))
				{
					rai::transaction transaction (node.node->store.environment, nullptr, true);
					node.node->store.delete_node_id (transaction);
				}
				success = node.node->copy_with_compaction (vacuum_path);
			}

			if (success)
			{
				// Note that these throw on failure
				std::cout << "Finalizing" << std::endl;
				boost::filesystem::remove (backup_path);
				boost::filesystem::rename (source_path, backup_path);
				boost::filesystem::rename (vacuum_path, source_path);
				std::cout << "Vacuum completed" << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Vacuum failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Vacuum failed" << std::endl;
		}
	}
	else if (vm.count ("snapshot"))
	{
		try
		{
			boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();

			auto source_path = data_path / "data.ldb";
			auto snapshot_path = data_path / "snapshot.ldb";

			std::cout << "Database snapshot of " << source_path << " to " << snapshot_path << " in progress" << std::endl;
			std::cout << "This may take a while..." << std::endl;

			bool success = false;
			{
				inactive_node node (data_path);
				if (vm.count ("unchecked_clear"))
				{
					rai::transaction transaction (node.node->store.environment, nullptr, true);
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("delete_node_id"))
				{
					rai::transaction transaction (node.node->store.environment, nullptr, true);
					node.node->store.delete_node_id (transaction);
				}
				success = node.node->copy_with_compaction (snapshot_path);
			}
			if (success)
			{
				std::cout << "Snapshot completed, This can be found at " << snapshot_path << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Snapshot failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Snapshot Failed" << std::endl;
		}
	}
	else if (vm.count ("unchecked_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
		inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, true);
		node.node->store.unchecked_clear (transaction);
		std::cerr << "Unchecked blocks deleted" << std::endl;
	}
	else if (vm.count ("delete_node_id"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
		inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, true);
		node.node->store.delete_node_id (transaction);
		std::cerr << "Deleted Node ID" << std::endl;
	}
	else if (vm.count ("diagnostics"))
	{
		inactive_node node (data_path);
		std::cout << "Testing hash function" << std::endl;
		rai::raw_key key;
		key.data.clear ();
		rai::send_block send (0, 0, 0, key, 0, 0);
		std::cout << "Testing key derivation function" << std::endl;
		rai::raw_key junk1;
		junk1.data.clear ();
		rai::uint256_union junk2 (0);
		rai::kdf kdf;
		kdf.phs (junk1, "", junk2);
		std::cout << "Dumping OpenCL information" << std::endl;
		bool error (false);
		rai::opencl_environment environment (error);
		if (!error)
		{
			environment.dump (std::cout);
			std::stringstream stream;
			environment.dump (stream);
			BOOST_LOG (node.logging.log) << stream.str ();
		}
		else
		{
			std::cout << "Error initializing OpenCL" << std::endl;
		}
	}
	else if (vm.count ("key_create"))
	{
		rai::keypair pair;
		std::cout << "Private: " << pair.prv.data.to_string () << std::endl
		          << "Public: " << pair.pub.to_string () << std::endl
		          << "Account: " << pair.pub.to_account () << std::endl;
	}
	else if (vm.count ("key_expand"))
	{
		if (vm.count ("key") == 1)
		{
			rai::uint256_union prv;
			prv.decode_hex (vm["key"].as<std::string> ());
			rai::uint256_union pub (rai::pub_key (prv));
			std::cout << "Private: " << prv.to_string () << std::endl
			          << "Public: " << pub.to_string () << std::endl
			          << "Account: " << pub.to_account () << std::endl;
		}
		else
		{
			std::cerr << "key_expand command requires one <key> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_add_adhoc"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					if (!wallet->enter_password (password))
					{
						rai::raw_key key;
						if (!key.data.decode_hex (vm["key"].as<std::string> ()))
						{
							rai::transaction transaction (wallet->store.environment, nullptr, true);
							wallet->store.insert_adhoc (transaction, key);
						}
						else
						{
							std::cerr << "Invalid key\n";
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_change_seed"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					if (!wallet->enter_password (password))
					{
						rai::raw_key key;
						if (!key.data.decode_hex (vm["key"].as<std::string> ()))
						{
							rai::transaction transaction (wallet->store.environment, nullptr, true);
							wallet->change_seed (transaction, key);
						}
						else
						{
							std::cerr << "Invalid key\n";
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_create"))
	{
		inactive_node node (data_path);
		rai::keypair key;
		std::cout << key.pub.to_string () << std::endl;
		auto wallet (node.node->wallets.create (key.pub));
		wallet->enter_initial_password ();
	}
	else if (vm.count ("wallet_decrypt_unsafe"))
	{
		if (vm.count ("wallet") == 1)
		{
			std::string password;
			if (vm.count ("password") == 1)
			{
				password = vm["password"].as<std::string> ();
			}
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				auto existing (node.node->wallets.items.find (wallet_id));
				if (existing != node.node->wallets.items.end ())
				{
					if (!existing->second->enter_password (password))
					{
						rai::transaction transaction (existing->second->store.environment, nullptr, false);
						rai::raw_key seed;
						existing->second->store.seed (seed, transaction);
						std::cout << boost::str (boost::format ("Seed: %1%\n") % seed.data.to_string ());
						for (auto i (existing->second->store.begin (transaction)), m (existing->second->store.end ()); i != m; ++i)
						{
							rai::account account (i->first);
							rai::raw_key key;
							auto error (existing->second->store.fetch (transaction, account, key));
							assert (!error);
							std::cout << boost::str (boost::format ("Pub: %1% Prv: %2%\n") % account.to_account () % key.data.to_string ());
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_decrypt_unsafe requires one <wallet> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_destroy"))
	{
		if (vm.count ("wallet") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				if (node.node->wallets.items.find (wallet_id) != node.node->wallets.items.end ())
				{
					node.node->wallets.destroy (wallet_id);
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_destroy requires one <wallet> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_import"))
	{
		if (vm.count ("file") == 1)
		{
			std::string filename (vm["file"].as<std::string> ());
			std::ifstream stream;
			stream.open (filename.c_str ());
			if (!stream.fail ())
			{
				std::stringstream contents;
				contents << stream.rdbuf ();
				std::string password;
				if (vm.count ("password") == 1)
				{
					password = vm["password"].as<std::string> ();
				}
				if (vm.count ("wallet") == 1)
				{
					rai::uint256_union wallet_id;
					if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
					{
						inactive_node node (data_path);
						auto existing (node.node->wallets.items.find (wallet_id));
						if (existing != node.node->wallets.items.end ())
						{
							if (existing->second->import (contents.str (), password))
							{
								std::cerr << "Unable to import wallet\n";
								ec = rai::error_cli::invalid_arguments;
							}
						}
						else
						{
							std::cerr << "Wallet doesn't exist\n";
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid wallet id\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "wallet_import requires one <wallet> option\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Unable to open <file>\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_import requires one <file> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_list"))
	{
		inactive_node node (data_path);
		for (auto i (node.node->wallets.items.begin ()), n (node.node->wallets.items.end ()); i != n; ++i)
		{
			std::cout << boost::str (boost::format ("Wallet ID: %1%\n") % i->first.to_string ());
			rai::transaction transaction (i->second->store.environment, nullptr, false);
			for (auto j (i->second->store.begin (transaction)), m (i->second->store.end ()); j != m; ++j)
			{
				std::cout << rai::uint256_union (j->first).to_account () << '\n';
			}
		}
	}
	else if (vm.count ("wallet_remove"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("account") == 1)
		{
			inactive_node node (data_path);
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					rai::account account_id;
					if (!account_id.decode_account (vm["account"].as<std::string> ()))
					{
						rai::transaction transaction (wallet->second->store.environment, nullptr, true);
						auto account (wallet->second->store.find (transaction, account_id));
						if (account != wallet->second->store.end ())
						{
							wallet->second->store.erase (transaction, account_id);
						}
						else
						{
							std::cerr << "Account not found in wallet\n";
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid account id\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet not found\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_remove command requires one <wallet> and one <account> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_representative_get"))
	{
		if (vm.count ("wallet") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					rai::transaction transaction (wallet->second->store.environment, nullptr, false);
					auto representative (wallet->second->store.representative (transaction));
					std::cout << boost::str (boost::format ("Representative: %1%\n") % representative.to_account ());
				}
				else
				{
					std::cerr << "Wallet not found\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_representative_get requires one <wallet> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_representative_set"))
	{
		if (vm.count ("wallet") == 1)
		{
			if (vm.count ("account") == 1)
			{
				rai::uint256_union wallet_id;
				if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
				{
					rai::account account;
					if (!account.decode_account (vm["account"].as<std::string> ()))
					{
						inactive_node node (data_path);
						auto wallet (node.node->wallets.items.find (wallet_id));
						if (wallet != node.node->wallets.items.end ())
						{
							rai::transaction transaction (wallet->second->store.environment, nullptr, true);
							wallet->second->store.representative_set (transaction, account);
						}
						else
						{
							std::cerr << "Wallet not found\n";
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid account\n";
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Invalid wallet id\n";
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "wallet_representative_set requires one <account> option\n";
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_representative_set requires one <wallet> option\n";
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("vote_dump") == 1)
	{
		inactive_node node (data_path);
		rai::transaction transaction (node.node->store.environment, nullptr, false);
		for (auto i (node.node->store.vote_begin (transaction)), n (node.node->store.vote_end ()); i != n; ++i)
		{
			auto vote (i->second);
			std::cerr << boost::str (boost::format ("%1%\n") % vote->to_json ());
		}
	}
	else
	{
		ec = rai::error_cli::unknown_command;
	}

	return ec;
}
