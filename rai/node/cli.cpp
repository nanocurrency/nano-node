#include <cryptopp/hex.h>
#include <cryptopp/sha.h>
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
	("clear_send_ids", "Remove all send IDs from the database (dangerous: not intended for production use)")
	("diagnostics", "Run internal diagnostics")
	("key_create", "Generates a adhoc random keypair and prints it to stdout")
	("key_expand", "Derive public key and account number from <key>")
	("seed_safe_export", "Export seed from <wallet> using wallet <password> and encrypt with <passphrase>")
	("seed_safe_import", "Import encrypted seed <file> into <wallet> using wallet <password> and decrypt with <passphrase>")
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
	("passphrase", boost::program_options::value<std::string> (), "Defines encryption <passphrase> for other commands")
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
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
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
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("delete_node_id"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.delete_node_id (transaction);
				}
				if (vm.count ("clear_send_ids"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->wallets.clear_send_ids (transaction);
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
			else
			{
				std::cerr << "Vacuum failed (copy_with_compaction returned false)" << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Vacuum failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Vacuum failed (unknown reason)" << std::endl;
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
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("delete_node_id"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.delete_node_id (transaction);
				}
				if (vm.count ("clear_send_ids"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->wallets.clear_send_ids (transaction);
				}
				success = node.node->copy_with_compaction (snapshot_path);
			}
			if (success)
			{
				std::cout << "Snapshot completed, This can be found at " << snapshot_path << std::endl;
			}
			else
			{
				std::cerr << "Snapshot Failed (copy_with_compaction returned false)" << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Snapshot failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Snapshot Failed (unknown reason)" << std::endl;
		}
	}
	else if (vm.count ("unchecked_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->store.unchecked_clear (transaction);
		std::cerr << "Unchecked blocks deleted" << std::endl;
	}
	else if (vm.count ("delete_node_id"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->store.delete_node_id (transaction);
		std::cerr << "Deleted Node ID" << std::endl;
	}
	else if (vm.count ("clear_send_ids"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : rai::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->wallets.clear_send_ids (transaction);
		std::cerr << "Send IDs deleted" << std::endl;
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
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						rai::raw_key key;
						if (!key.data.decode_hex (vm["key"].as<std::string> ()))
						{
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
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						rai::raw_key key;
						if (!key.data.decode_hex (vm["key"].as<std::string> ()))
						{
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
					auto transaction (existing->second->wallets.tx_begin_write ());
					if (!existing->second->enter_password (transaction, password))
					{
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
							if (rai::pub_key (key.data) != account)
							{
								std::cerr << boost::str (boost::format ("Invalid private key %1%\n") % key.data.to_string ());
							}
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
			auto transaction (i->second->wallets.tx_begin_read ());
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
						auto transaction (wallet->second->wallets.tx_begin_write ());
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
					auto transaction (wallet->second->wallets.tx_begin_read ());
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
							auto transaction (wallet->second->wallets.tx_begin_write ());
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
		auto transaction (node.node->store.tx_begin_read ());
		for (auto i (node.node->store.vote_begin (transaction)), n (node.node->store.vote_end ()); i != n; ++i)
		{
			auto vote (i->second);
			std::cerr << boost::str (boost::format ("%1%\n") % vote->to_json ());
		}
	}
	else if (vm.count ("seed_safe_export") == 1)
	{
		if (vm.count ("password") == 1 && vm.count ("wallet") == 1 && vm.count ("passphrase") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password = vm["password"].as<std::string> ();
				std::string passphrase = vm["passphrase"].as<std::string> ();
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					auto transaction (node.node->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						rai::uint256_union derived_key_iv;
						rai::random_pool.GenerateBlock (derived_key_iv.bytes.data (), derived_key_iv.bytes.size ());
						std::string derived_key_iv_hex;
						derived_key_iv.encode_hex (derived_key_iv_hex);
						rai::raw_key derived_key;
						wallet->store.kdf.phs (derived_key, passphrase, derived_key_iv);
						std::string passphrase_hex;
						derived_key.data.encode_hex (passphrase_hex);
						rai::uint128_union seed_iv;
						rai::random_pool.GenerateBlock (seed_iv.bytes.data (), seed_iv.bytes.size ());
						std::string seed_iv_hex;
						seed_iv.encode_hex (seed_iv_hex);
						rai::raw_key seed;
						wallet->store.seed (seed, transaction);
						// The sha256 of the seed is used as a checksum, e.g. to verify that the decrypted seed
						// is the intended one.
						CryptoPP::SHA256 hash;
						CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
						hash.CalculateDigest (digest, seed.data.bytes.data (), seed.data.bytes.size ());
						CryptoPP::HexEncoder encoder;
						std::string checksum_hex;
						encoder.Attach (new CryptoPP::StringSink (checksum_hex));
						encoder.Put (digest, CryptoPP::SHA256::DIGESTSIZE);
						encoder.MessageEnd ();
						seed.data.encrypt (seed, derived_key, seed_iv);
						std::string seed_encrypted_hex;
						seed.data.encode_hex (seed_encrypted_hex);
						boost::property_tree::ptree seed_export_json;
						seed_export_json.add ("type_key", "ARGON2-IV256");
						seed_export_json.add ("type_cipher", "AES256-CTR-IV128");
						seed_export_json.add ("type_checksum", "SHA256");
						seed_export_json.add ("key_iv", derived_key_iv_hex);
						seed_export_json.add ("seed_iv", seed_iv_hex);
						seed_export_json.add ("seed_encrypted", seed_encrypted_hex);
						seed_export_json.add ("checksum", checksum_hex);
						boost::property_tree::write_json (std::cout, seed_export_json);
					}
					else
					{
						std::cerr << "Invalid wallet password" << std::endl;
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist" << std::endl;
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id" << std::endl;
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "seed_safe_export requires the <wallet>, <password> and <passphrase> options" << std::endl;
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("seed_safe_import") == 1)
	{
		if (vm.count ("password") == 1 && vm.count ("wallet") == 1 && vm.count ("passphrase") == 1 && vm.count ("file") == 1)
		{
			rai::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password = vm["password"].as<std::string> ();
				std::string passphrase = vm["passphrase"].as<std::string> ();
				std::string filename = vm["file"].as<std::string> ();
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					auto transaction (node.node->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						try
						{
							boost::property_tree::ptree json;
							boost::property_tree::read_json (filename, json);
							std::string type_key = json.get<std::string> ("type_key");
							std::string type_cipher = json.get<std::string> ("type_cipher");
							std::string type_checksum = json.get<std::string> ("type_checksum");
							std::string input_key_iv = json.get<std::string> ("key_iv");
							std::string input_seed_iv = json.get<std::string> ("seed_iv");
							std::string input_seed_encrypted = json.get<std::string> ("seed_encrypted");
							std::string input_checksum = json.get<std::string> ("checksum");
							if (type_key == "ARGON2-IV256" && type_cipher == "AES256-CTR-IV128" && type_checksum == "SHA256")
							{
								// Run the passphrase through deriviation using the imported key iv
								rai::uint256_union derived_key_iv;
								derived_key_iv.decode_hex (input_key_iv);
								rai::raw_key derived_key;
								wallet->store.kdf.phs (derived_key, passphrase, derived_key_iv);
								rai::uint128_union enc_iv;
								rai::uint256_union seed_imported;
								if (!seed_imported.decode_hex (input_seed_encrypted) && !enc_iv.decode_hex (input_seed_iv))
								{
									// We're going to print both the old and the new seed as a safety measure, in case the user
									// imported into the wrong wallet.
									rai::raw_key current_seed;
									wallet->store.seed (current_seed, transaction);
									std::string current_seed_hex;
									current_seed.data.encode_hex (current_seed_hex);
									rai::raw_key seed_decryped;
									seed_decryped.decrypt (seed_imported, derived_key, enc_iv);
									std::string new_seed_hex;
									seed_decryped.data.encode_hex (new_seed_hex);
									// Recalculate checksum
									CryptoPP::SHA256 hash;
									CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
									hash.CalculateDigest (digest, seed_decryped.data.bytes.data (), seed_decryped.data.bytes.size ());
									CryptoPP::HexEncoder encoder;
									std::string checksum_hex;
									encoder.Attach (new CryptoPP::StringSink (checksum_hex));
									encoder.Put (digest, CryptoPP::SHA256::DIGESTSIZE);
									encoder.MessageEnd ();
									if (checksum_hex == input_checksum)
									{
										std::cout << "Old seed: " << current_seed_hex << std::endl;
										std::cout << "New seed: " << new_seed_hex << std::endl;
										wallet->store.seed_set (transaction, seed_decryped);
										std::cout << "Changed seed successfully" << std::endl;
									}
									else
									{
										std::cerr << "Invalid seed checksum. Check passphrase and input file and try again." << std::endl;
										ec = rai::error_cli::invalid_arguments;
									}
								}
								else
								{
									std::cerr << "Invalid hex input" << std::endl;
									ec = rai::error_cli::invalid_arguments;
								}
							}
							else
							{
								std::cerr << "Unsupported seed import type" << std::endl;
								ec = rai::error_cli::invalid_arguments;
							}
						}
						catch (std::runtime_error const & ex)
						{
							std::cerr << "Could not import seed from json file: " << ex.what () << std::endl;
							ec = rai::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid wallet password" << std::endl;
						ec = rai::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist" << std::endl;
					ec = rai::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id" << std::endl;
				ec = rai::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "seed_safe_import requires the <wallet>, <password>, <passphrase> and <file> options" << std::endl;
			ec = rai::error_cli::invalid_arguments;
		}
	}
	else
	{
		ec = rai::error_cli::unknown_command;
	}

	return ec;
}
