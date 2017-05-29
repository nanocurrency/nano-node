#include <gtest/gtest.h>

#include <rai/node/testing.hpp>
#include <rai/node/rpc.hpp>

#include <beast/http.hpp>
#include <beast/http/string_body.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/thread.hpp>

class test_response
{
public:
	test_response (boost::property_tree::ptree const & request_a, rai::rpc & rpc_a, boost::asio::io_service & service_a) :
	request (request_a),
	sock (service_a),
	status (0)
	{
		sock.async_connect (rai::tcp_endpoint (boost::asio::ip::address_v6::loopback (), rpc_a.config.port), [this] (boost::system::error_code const & ec)
		{
			if (!ec)
			{
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, request);
				req.method ("POST");
				req.target ("/");
				req.version = 11;
				ostream.flush ();
				req.body = ostream.str ();
				beast::http::prepare(req);
				beast::http::async_write (sock, req, [this] (boost::system::error_code & ec)
				{
					if (!ec)
					{
						beast::http::async_read(sock, sb, resp, [this] (boost::system::error_code & ec)
						{
							if (!ec)
							{
								std::stringstream body (resp.body);
								try
								{
									boost::property_tree::read_json (body, json);
									status = 200;
								}
								catch (std::exception & e)
								{
									status = 500;
								}
							}
							else
							{
								status = 400;
							};
						});
					}
					else
					{
						status = 600;
					}
				});
			}
			else
			{
				status = 400;
			}
		});
	}
	boost::property_tree::ptree const & request;
	boost::asio::ip::tcp::socket sock;
	boost::property_tree::ptree json;
	beast::flat_buffer sb;
	beast::http::request<beast::http::string_body> req;
	beast::http::response<beast::http::string_body> resp;
	int status;
};

TEST (rpc, account_balance)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "account_balance");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response.status);
	std::string balance_text (response.json.get <std::string> ("balance"));
	ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
	std::string pending_text (response.json.get <std::string> ("pending"));
	ASSERT_EQ ("0", pending_text);
}

TEST (rpc, account_block_count)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "account_block_count");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response.status);
	std::string block_count_text (response.json.get <std::string> ("block_count"));
	ASSERT_EQ ("1", block_count_text);
}

TEST (rpc, account_create)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_create");
	request.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response.status);
	auto account_text (response.json.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (system.wallet (0)->exists (account));
}

TEST (rpc, account_weight)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::block_hash latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
    rai::change_block block (latest, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
	ASSERT_EQ (rai::process_result::progress, node1.process (block).code);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "account_weight");
    request.put ("account", key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response.status);
    std::string balance_text (response.json.get <std::string> ("weight"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, wallet_contains)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_contains");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string exists_text (response.json.get <std::string> ("exists"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_doesnt_contain)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service,  *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_contains");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string exists_text (response.json.get <std::string> ("exists"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, validate_account_number)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service,  *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "validate_account_number");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    std::string exists_text (response.json.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, validate_account_invalid)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service,  *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    std::string account;
    rai::test_genesis_key.pub.encode_account (account);
    account [0] ^= 0x1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "validate_account_number");
    request.put ("account", account);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string exists_text (response.json.get <std::string> ("valid"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, send)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
    request.put ("destination", rai::test_genesis_key.pub.to_account ());
    request.put ("amount", "100");
	std::thread thread2 ([&system] ()
	{
		auto iterations (0);
		while (system.nodes [0]->balance (rai::test_genesis_key.pub) == rai::genesis_amount)
		{
			system.poll ();
			++iterations;
			ASSERT_LT (iterations, 200);
		}
	});
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string block_text (response.json.get <std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (system.nodes [0]->ledger.block_exists (block));
	thread2.join ();
}

TEST (rpc, send_fail)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
    request.put ("destination", rai::test_genesis_key.pub.to_account ());
    request.put ("amount", "100");
	auto done (false);
	std::thread thread2 ([&system, &done] ()
	{
		auto iterations (0);
		while (!done)
		{
			system.poll ();
			++iterations;
			ASSERT_LT (iterations, 200);
		}
	});
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
	done = true;
    ASSERT_EQ (200, response.status);
    std::string block_text (response.json.get <std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (block.is_zero ());
	thread2.join ();
}

TEST (rpc, stop)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "stop");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	};
	ASSERT_FALSE (system.nodes [0]->network.on);
}

TEST (rpc, wallet_add)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    rai::keypair key1;
    std::string key_text;
    key1.prv.data.encode_hex (key_text);
	system.wallet (0)->insert_adhoc (key1.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_add");
    request.put ("key", key_text);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("account"));
    ASSERT_EQ (account_text1, key1.pub.to_account ());
}

TEST (rpc, wallet_password_valid)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_valid");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_password_change)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_change");
    request.put ("password", "test");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("changed"));
    ASSERT_EQ (account_text1, "1");
    ASSERT_TRUE (system.wallet (0)->valid_password ());
    ASSERT_TRUE (system.wallet (0)->enter_password (""));
    ASSERT_FALSE (system.wallet (0)->valid_password ());
    ASSERT_FALSE (system.wallet (0)->enter_password ("test"));
    ASSERT_TRUE (system.wallet (0)->valid_password ());
}

TEST (rpc, wallet_password_enter)
{
    rai::system system (24000, 1);
	auto iterations (0);
	rai::raw_key password_l;
	password_l.data.clear ();
	while (password_l.data == 0)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
		system.wallet (0)->store.password.value (password_l);
	}
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_enter");
    request.put ("password", "");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_representative)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_representative");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("representative"));
    ASSERT_EQ (account_text1, rai::genesis_account.to_account ());
}

TEST (rpc, wallet_representative_set)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    rai::keypair key;
    request.put ("action", "wallet_representative_set");
    request.put ("representative", key.pub.to_account ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (key.pub, system.nodes [0]->wallets.items.begin ()->second->store.representative (transaction));
}

TEST (rpc, account_list)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "account_list");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & accounts_node (response.json.get_child ("accounts"));
    std::vector <rai::uint256_union> accounts;
    for (auto i (accounts_node.begin ()), j (accounts_node.end ()); i != j; ++i)
    {
        auto account (i->second.get <std::string> (""));
        rai::uint256_union number;
        ASSERT_FALSE (number.decode_account (account));
        accounts.push_back (number);
    }
    ASSERT_EQ (2, accounts.size ());
    for (auto i (accounts.begin ()), j (accounts.end ()); i != j; ++i)
    {
        ASSERT_TRUE (system.wallet (0)->exists (*i));
    }
}

TEST (rpc, wallet_key_valid)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_key_valid");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string exists_text (response.json.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_create)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "wallet_create");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string wallet_text (response.json.get <std::string> ("wallet"));
    rai::uint256_union wallet_id;
    ASSERT_FALSE (wallet_id.decode_hex (wallet_text));
    ASSERT_NE (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
}

TEST (rpc, wallet_export)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "wallet_export");
    request.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string wallet_json (response.json.get <std::string> ("json"));
    bool error (false);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
	rai::kdf kdf;
    rai::wallet_store store (error, kdf, transaction, rai::genesis_account, 1, "0", wallet_json);
    ASSERT_FALSE (error);
    ASSERT_TRUE (store.exists (transaction, rai::test_genesis_key.pub));
}

TEST (rpc, wallet_destroy)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "wallet_destroy");
    request.put ("wallet", wallet_id.to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    ASSERT_EQ (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
}

TEST (rpc, account_move)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    auto destination (system.wallet (0));
    rai::keypair key;
	destination->insert_adhoc (rai::test_genesis_key.prv);
    rai::keypair source_id;
    auto source (system.nodes [0]->wallets.create (source_id.pub));
	source->insert_adhoc (key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "account_move");
    request.put ("wallet", wallet_id.to_string ());
    request.put ("source", source_id.pub.to_string ());
    boost::property_tree::ptree keys;
    boost::property_tree::ptree entry;
    entry.put ("", key.pub.to_string ());
    keys.push_back (std::make_pair ("", entry));
    request.add_child ("accounts", keys);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    ASSERT_EQ ("1", response.json.get <std::string> ("moved"));
    ASSERT_TRUE (destination->exists (key.pub));
    ASSERT_TRUE (destination->exists (rai::test_genesis_key.pub));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (source->store.end (), source->store.begin (transaction));
}

TEST (rpc, block)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service,  *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "block");
	request.put ("hash", system.nodes [0]->latest (rai::genesis_account).to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	auto contents (response.json.get <std::string> ("contents"));
    ASSERT_FALSE (contents.empty ());
}

TEST (rpc, block_account)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	rai::genesis genesis;
    boost::property_tree::ptree request;
    request.put ("action", "block_account");
	request.put ("hash", genesis.hash ().to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text (response.json.get <std::string> ("account"));
    rai::account account;
    ASSERT_FALSE (account.decode_account (account_text));
}

TEST (rpc, chain)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "chain");
	request.put ("block", block->hash().to_string ());
	request.put ("count", std::to_string (std::numeric_limits <uint64_t>::max ()));
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & blocks_node (response.json.get_child ("blocks"));
	std::vector <rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block->hash(), blocks [0]);
	ASSERT_EQ (genesis, blocks [1]);
}

TEST (rpc, chain_limit)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "chain");
	request.put ("block", block->hash().to_string ());
	request.put ("count", 1);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & blocks_node (response.json.get_child ("blocks"));
	std::vector <rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block->hash(), blocks [0]);
}

TEST (rpc, frontier)
{
    rai::system system (24000, 1);
	std::unordered_map <rai::account, rai::block_hash> source;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source [key.pub] = key.prv.data;
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (std::numeric_limits <uint64_t>::max ()));
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & frontiers_node (response.json.get_child ("frontiers"));
    std::unordered_map <rai::account, rai::block_hash> frontiers;
    for (auto i (frontiers_node.begin ()), j (frontiers_node.end ()); i != j; ++i)
    {
        rai::account account;
		account.decode_account (i->first);
		rai::block_hash frontier;
		frontier.decode_hex (i->second.get <std::string> (""));
        frontiers [account] = frontier;
    }
	ASSERT_EQ (1, frontiers.erase (rai::test_genesis_key.pub));
	ASSERT_EQ (source, frontiers);
}

TEST (rpc, frontier_limited)
{
    rai::system system (24000, 1);
	std::unordered_map <rai::account, rai::block_hash> source;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source [key.pub] = key.prv.data;
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (100));
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & frontiers_node (response.json.get_child ("frontiers"));
	ASSERT_EQ (100, frontiers_node.size ());
}

TEST (rpc, frontier_startpoint)
{
    rai::system system (24000, 1);
	std::unordered_map <rai::account, rai::block_hash> source;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source [key.pub] = key.prv.data;
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", source.begin ()->first.to_account ());
	request.put ("count", std::to_string (1));
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & frontiers_node (response.json.get_child ("frontiers"));
	ASSERT_EQ (1, frontiers_node.size ());
	ASSERT_EQ (source.begin ()->first.to_account (), frontiers_node.begin ()->first);
}

TEST (rpc, history)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto change (system.wallet (0)->change_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub));
	ASSERT_NE (nullptr, change);
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, system.nodes [0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (0)->receive_action (static_cast <rai::send_block &>(*send), rai::test_genesis_key.pub, system.nodes [0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "history");
	request.put ("hash", receive->hash().to_string ());
	request.put ("count", 100);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	std::vector <std::tuple <std::string, std::string, std::string, std::string>> history_l;
    auto & history_node (response.json.get_child ("history"));
	for (auto i (history_node.begin ()), n (history_node.end ()); i != n; ++i)
	{
		history_l.push_back (std::make_tuple (i->second.get <std::string> ("type"), i->second.get <std::string> ("account"), i->second.get <std::string> ("amount"), i->second.get <std::string> ("hash")));
	}
	ASSERT_EQ (3, history_l.size ());
	ASSERT_EQ ("receive", std::get <0> (history_l [0]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get <1> (history_l [0]));
	ASSERT_EQ (system.nodes [0]->config.receive_minimum.to_string_dec (), std::get <2> (history_l [0]));
	ASSERT_EQ (receive->hash ().to_string (), std::get <3> (history_l [0]));
	ASSERT_EQ ("send", std::get <0> (history_l [1]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get <1> (history_l [1]));
	ASSERT_EQ (system.nodes [0]->config.receive_minimum.to_string_dec (), std::get <2> (history_l [1]));
	ASSERT_EQ (send->hash ().to_string (), std::get <3> (history_l [1]));
	rai::genesis genesis;
	ASSERT_EQ ("receive", std::get <0> (history_l [2]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get <1> (history_l [2]));
	ASSERT_EQ (rai::genesis_amount.convert_to <std::string> (), std::get <2> (history_l [2]));
	ASSERT_EQ (genesis.hash ().to_string (), std::get <3> (history_l [2]));
}

TEST (rpc, history_count)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto change (system.wallet (0)->change_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub));
	ASSERT_NE (nullptr, change);
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, system.nodes [0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (0)->receive_action (static_cast <rai::send_block &>(*send), rai::test_genesis_key.pub, system.nodes [0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "history");
	request.put ("hash", receive->hash().to_string ());
	request.put ("count", 1);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & history_node (response.json.get_child ("history"));
	ASSERT_EQ (1, history_node.size ());
}

TEST (rpc, process_block)
{
    rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	ASSERT_EQ (send.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
}

TEST (rpc, process_block_no_work)
{
    rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
	send.block_work_set(0);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	ASSERT_FALSE (response.json.get <std::string> ("error").empty ());
}

TEST (rpc, keepalive)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
    node1->start ();
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "keepalive");
    auto address (boost::str (boost::format ("%1%") % node1->network.endpoint ().address ()));
    auto port (boost::str (boost::format ("%1%") % node1->network.endpoint ().port ()));
	request.put ("address", address);
	request.put ("port", port);
	ASSERT_FALSE (system.nodes [0]->peers.known_peer (node1->network.endpoint ()));
	ASSERT_EQ (0, system.nodes [0]->peers.size());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	auto iterations (0);
	while (!system.nodes [0]->peers.known_peer (node1->network.endpoint ()))
	{
		ASSERT_EQ (0, system.nodes [0]->peers.size());
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (rpc, payment_init)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "payment_init");
	request.put ("wallet", wallet_id.pub.to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	ASSERT_EQ ("Ready", response.json.get <std::string> ("status"));
}

TEST (rpc, payment_begin_end)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	auto root1 (system.nodes [0]->ledger.latest_root (rai::transaction (wallet->store.environment, nullptr, false), account));
	uint64_t work (0);
	auto iteration (0);
	ASSERT_TRUE (system.work.work_validate (root1, work));
	while (system.work.work_validate (root1, work))
	{
		system.poll ();
		ASSERT_FALSE (wallet->store.work_get (rai::transaction (wallet->store.environment, nullptr, false), account, work));
		++iteration;
		ASSERT_LT (iteration, 200);
	}
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
    boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	test_response response2 (request2, rpc, system.service);
	while (response2.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response2.status);
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_NE (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	rpc.stop ();
	system.stop ();
}

TEST (rpc, payment_end_nonempty)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->init_free_accounts (rai::transaction (node1->store.environment, nullptr, false));
	auto wallet_id (node1->wallets.items.begin ()->first);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_end");
	request1.put ("wallet", wallet_id.to_string ());
	request1.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_FALSE (response1.json.get <std::string> ("error").empty ());
}

TEST (rpc, payment_zero_balance)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->init_free_accounts (rai::transaction (node1->store.environment, nullptr, false));
	auto wallet_id (node1->wallets.items.begin ()->first);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.to_string ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_NE (rai::test_genesis_key.pub, account);
}

TEST (rpc, payment_begin_reuse)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
    boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	test_response response2 (request2, rpc, system.service);
	while (response2.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response2.status);
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_NE (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	test_response response3 (request1, rpc, system.service);
	while (response3.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response3.status);
	auto account2_text (response1.json.get <std::string> ("account"));
	rai::uint256_union account2;
	ASSERT_FALSE (account2.decode_account (account2_text));
	ASSERT_EQ (account, account2);
}

TEST (rpc, payment_begin_locked)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	{
		rai::transaction transaction (wallet->store.environment, nullptr, true);
		wallet->store.rekey(transaction, "1");
		ASSERT_TRUE (wallet->store.attempt_password(transaction, ""));
	}
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_FALSE (response1.json.get <std::string> ("error").empty ());
}

TEST (rpc, DISABLED_payment_wait)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_wait");
	request1.put ("account", key.pub.to_account ());
	request1.put ("amount", rai::amount (rai::Mxrb_ratio).to_string_dec ());
	request1.put ("timeout", "100");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("nothing", response1.json.get <std::string> ("status"));
	request1.put ("timeout", "100000");
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mxrb_ratio);
	system.alarm.add (std::chrono::system_clock::now () + std::chrono::milliseconds(500), [&] ()
	{
		system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mxrb_ratio);
	});
	test_response response2 (request1, rpc, system.service);
	while (response2.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response2.status);
	ASSERT_EQ ("success", response2.json.get <std::string> ("status"));
	request1.put ("amount", rai::amount (rai::Mxrb_ratio * 2).to_string_dec ());
	test_response response3 (request1, rpc, system.service);
	while (response3.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response3.status);
	ASSERT_EQ ("success", response2.json.get <std::string> ("status"));
}

TEST (rpc, peers)
{
    rai::system system (24000, 2);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "peers");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & peers_node (response.json.get_child ("peers"));
	ASSERT_EQ (1, peers_node.size ());
}

TEST (rpc, pending)
{
    rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "pending");
	request.put ("account", key1.pub.to_account ());
	request.put ("count", "100");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & blocks_node (response.json.get_child ("blocks"));
	ASSERT_EQ (1, blocks_node.size ());
	rai::block_hash hash1 (blocks_node.begin ()->second.get <std::string> (""));
	ASSERT_EQ (block1->hash (), hash1);
}

TEST (rpc_config, serialization)
{
	rai::rpc_config config1;
	config1.address = boost::asio::ip::address_v6::any();
	config1.port = 10;
	config1.enable_control = true;
	config1.frontier_request_limit = 8192;
	config1.chain_request_limit = 4096;
	boost::property_tree::ptree tree;
	config1.serialize_json (tree);
	rai::rpc_config config2;
	ASSERT_NE (config2.address, config1.address);
	ASSERT_NE (config2.port, config1.port);
	ASSERT_NE (config2.enable_control, config1.enable_control);
	ASSERT_NE (config2.frontier_request_limit, config1.frontier_request_limit);
	ASSERT_NE (config2.chain_request_limit, config1.chain_request_limit);
	config2.deserialize_json (tree);
	ASSERT_EQ (config2.address, config1.address);
	ASSERT_EQ (config2.port, config1.port);
	ASSERT_EQ (config2.enable_control, config1.enable_control);
	ASSERT_EQ (config2.frontier_request_limit, config1.frontier_request_limit);
	ASSERT_EQ (config2.chain_request_limit, config1.chain_request_limit);
}

TEST (rpc, search_pending)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto wallet (system.nodes [0]->wallets.items.begin ()->first.to_string ());
	rai::send_block block (system.nodes [0]->latest (rai::test_genesis_key.pub), rai::test_genesis_key.pub, rai::genesis_amount - system.nodes [0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (rai::transaction (system.nodes [0]->store.environment, nullptr, true), block).code);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "search_pending");
	request.put ("wallet", wallet);
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	auto iterations (0);
	while (system.nodes [0]->balance (rai::test_genesis_key.pub) != rai::genesis_amount)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (rpc, version)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "version");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("rpc_version"));
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("6", response1.json.get <std::string> ("store_version"));
	ASSERT_EQ (boost::str (boost::format ("RaiBlocks %1%.%2%.%3%") % RAIBLOCKS_VERSION_MAJOR % RAIBLOCKS_VERSION_MINOR % RAIBLOCKS_VERSION_PATCH), response1.json.get <std::string> ("node_vendor"));
	auto & headers (response1.resp.fields);
	auto access_control (std::find_if (headers.begin (), headers.end (), [] (decltype (*headers.begin ()) & header_a) { return boost::iequals (header_a.first, "Access-Control-Allow-Origin"); }));
	ASSERT_NE (headers.end (), access_control);
	ASSERT_EQ ("*", access_control->second);
}

TEST (rpc, work_generate)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash1 (1);
    boost::property_tree::ptree request1;
	request1.put ("action", "work_generate");
	request1.put ("hash", hash1.to_string ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	auto work1 (response1.json.get <std::string> ("work"));
	uint64_t work2;
	ASSERT_FALSE (rai::from_string_hex (work1, work2));
	ASSERT_FALSE (system.work.work_validate (hash1, work2));
}

TEST (rpc, work_cancel)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash1 (1);
    boost::property_tree::ptree request1;
	request1.put ("action", "work_cancel");
	request1.put ("hash", hash1.to_string ());
	boost::optional <uint64_t> work;
	std::thread thread ([&] ()
	{
		work = system.work.generate (hash1);
	});
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response1.status);
	thread.join ();
}

TEST (rpc, work_peer_bad)
{
    rai::system system (24000, 2);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
	node2.config.work_peers.push_back (std::make_pair (boost::asio::ip::address_v6::any (), 0));
	rai::block_hash hash1 (1);
	uint64_t work (0);
	node2.generate_work (hash1, [&work] (uint64_t work_a)
	{
		work = work_a;
	});
	while (system.work.work_validate (hash1, work))
	{
		system.poll ();
	}
}

TEST (rpc, work_peer_one)
{
    rai::system system (24000, 2);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
	node2.config.work_peers.push_back (std::make_pair (node1.network.endpoint ().address (), rpc.config.port));
	rai::keypair key1;
	uint64_t work (0);
	node2.generate_work (key1.pub, [&work] (uint64_t work_a)
	{
		work = work_a;
	});
	while (system.work.work_validate (key1.pub, work))
	{
		system.poll ();
	}
}

TEST (rpc, work_peer_many)
{
    rai::system system1 (24000, 1);
    rai::system system2 (24001, 1);
    rai::system system3 (24002, 1);
    rai::system system4 (24003, 1);
	rai::node_init init1;
    auto & node1 (*system1.nodes [0]);
    auto & node2 (*system2.nodes [0]);
    auto & node3 (*system3.nodes [0]);
    auto & node4 (*system4.nodes [0]);
	rai::keypair key;
	rai::rpc_config config2 (true);
	config2.port += 0;
    rai::rpc rpc2 (system2.service, node2, config2);
	rpc2.start ();
	rai::rpc_config config3 (true);
	config3.port += 1;
    rai::rpc rpc3 (system3.service, node3, config3);
	rpc3.start ();
	rai::rpc_config config4 (true);
	config4.port += 2;
    rai::rpc rpc4 (system4.service, node4, config4);
	rpc4.start ();
	node1.config.work_peers.push_back (std::make_pair (node2.network.endpoint ().address (), rpc2.config.port));
	node1.config.work_peers.push_back (std::make_pair (node3.network.endpoint ().address (), rpc3.config.port));
	node1.config.work_peers.push_back (std::make_pair (node4.network.endpoint ().address (), rpc4.config.port));
	for (auto i (0); i < 10; ++i)
	{
		rai::keypair key1;
		uint64_t work (0);
		node1.generate_work (key1.pub, [&work] (uint64_t work_a)
		{
			work = work_a;
		});
		while (system1.work.work_validate (key1.pub, work))
		{
			system1.poll ();
			system2.poll ();
			system3.poll ();
			system4.poll ();
		}
	}
}

TEST (rpc, block_count)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "block_count");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("count"));
	ASSERT_EQ ("0", response1.json.get <std::string> ("unchecked"));
}

TEST (rpc, frontier_count)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "frontier_count");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("count"));
}

TEST (rpc, available_supply)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "available_supply");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("0", response1.json.get <std::string> ("available"));
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	test_response response2 (request1, rpc, system.service);
	while (response2.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response2.status);
	ASSERT_EQ ("1", response2.json.get <std::string> ("available"));
}

TEST (rpc, mrai_to_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "mrai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::Mxrb_ratio.convert_to <std::string> (), response1.json.get <std::string> ("amount"));
}

TEST (rpc, mrai_from_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "mrai_from_raw");
	request1.put ("amount", rai::Mxrb_ratio.convert_to <std::string> ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("amount"));
}

TEST (rpc, krai_to_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "krai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::kxrb_ratio.convert_to <std::string> (), response1.json.get <std::string> ("amount"));
}

TEST (rpc, krai_from_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "krai_from_raw");
	request1.put ("amount", rai::kxrb_ratio.convert_to <std::string> ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("amount"));
}

TEST (rpc, rai_to_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "rai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::xrb_ratio.convert_to <std::string> (), response1.json.get <std::string> ("amount"));
}

TEST (rpc, rai_from_raw)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request1;
	request1.put ("action", "rai_from_raw");
	request1.put ("amount", rai::xrb_ratio.convert_to <std::string> ());
	test_response response1 (request1, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get <std::string> ("amount"));
}

TEST (rpc, account_representative)
{
    rai::system system (24000, 1);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    std::string wallet;
    request.put ("account", rai::genesis_account.to_account ());
    request.put ("action", "account_representative");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string account_text1 (response.json.get <std::string> ("representative"));
    ASSERT_EQ (account_text1, rai::genesis_account.to_account ());
}

TEST (rpc, account_representative_set)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	rai::keypair rep;
    request.put ("account", rai::genesis_account.to_account ());
	request.put ("representative", rep.pub.to_account ());
	request.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
    request.put ("action", "account_representative_set");
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    std::string block_text1 (response.json.get <std::string> ("block"));
	rai::block_hash hash;
	ASSERT_FALSE (hash.decode_hex (block_text1));
	ASSERT_FALSE (hash.is_zero ());
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_TRUE (system.nodes [0]->store.block_exists (transaction, hash));
	ASSERT_EQ (rep.pub, system.nodes [0]->store.block_get (transaction, hash)->representative ());
}

TEST (rpc, bootstrap)
{
    rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto latest (system1.nodes [0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, rai::genesis_account, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system1.nodes [0]->generate_work (latest));
	{
		rai::transaction transaction (system1.nodes [0]->store.environment, nullptr, true);
		ASSERT_EQ (rai::process_result::progress, system1.nodes [0]->ledger.process (transaction, send).code);
	}
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "bootstrap");
    request.put ("address", "::ffff:127.0.0.1");
	request.put ("port", system1.nodes [0]->network.endpoint ().port ());
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
	auto iterations (0);
	while (system0.nodes [0]->latest (rai::genesis_account) != system1.nodes [0]->latest (rai::genesis_account))
	{
		system0.poll ();
		system1.poll ();
		++iterations;
		ASSERT_GT (200, iterations);
	}
}

TEST (rpc, account_remove)
{
    rai::system system0 (24000, 1);
	auto key1 (system0.wallet (0)->deterministic_insert ());
	ASSERT_TRUE (system0.wallet (0)->exists (key1));
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "account_remove");
    request.put ("wallet", system0.nodes [0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", key1.to_account ());
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_FALSE (system0.wallet (0)->exists (key1));
}

TEST (rpc, representatives)
{
    rai::system system0 (24000, 1);
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "representatives");
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & representatives_node (response.json.get_child ("representatives"));
	std::vector <rai::account> representatives;
	for (auto i (representatives_node.begin ()), n (representatives_node.end ()); i != n; ++i)
	{
		rai::account account;
		ASSERT_FALSE (account.decode_account (i->first));
		representatives.push_back (account);
	}
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (rai::genesis_account, representatives [0]);
}

TEST (rpc, wallet_change_seed)
{
    rai::system system0 (24000, 1);
	rai::keypair seed;
	{
		rai::transaction transaction (system0.nodes [0]->store.environment, nullptr, false);
		rai::raw_key seed0;
		system0.wallet (0)->store.seed (seed0, transaction);
		ASSERT_NE (seed.pub, seed0.data);
	}
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "wallet_change_seed");
    request.put ("wallet", system0.nodes [0]->wallets.items.begin ()->first.to_string ());
	request.put ("seed", seed.pub.to_string ());
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
    ASSERT_EQ (200, response.status);
	{
		rai::transaction transaction (system0.nodes [0]->store.environment, nullptr, false);
		rai::raw_key seed0;
		system0.wallet (0)->store.seed (seed0, transaction);
		ASSERT_EQ (seed.pub, seed0.data);
	}
}

TEST (rpc, wallet_frontiers)
{
    rai::system system0 (24000, 1);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "wallet_frontiers");
    request.put ("wallet", system0.nodes [0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & frontiers_node (response.json.get_child ("frontiers"));
	std::vector <rai::account> frontiers;
	for (auto i (frontiers_node.begin ()), n (frontiers_node.end ()); i != n; ++i)
	{
		frontiers.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (1, frontiers.size ());
	ASSERT_EQ (system0.nodes [0]->latest (rai::genesis_account), frontiers [0]);
}

TEST (rpc, work_validate)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash (1);
	uint64_t work1 (node1.generate_work (hash));
	boost::property_tree::ptree request;
	request.put ("action", "work_validate");
	request.put ("hash", hash.to_string ());
	request.put ("work", rai::to_string_hex (work1));
	test_response response1 (request, rpc, system.service);
	while (response1.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response1.status);
	std::string validate_text1 (response1.json.get <std::string> ("valid"));
	ASSERT_EQ ("1", validate_text1);
	uint64_t work2 (0);
	request.put ("work", rai::to_string_hex (work2));
	test_response response2 (request, rpc, system.service);
	while (response2.status == 0)
	{
		system.poll ();
	}
	ASSERT_EQ (200, response2.status);
	std::string validate_text2 (response2.json.get <std::string> ("valid"));
	ASSERT_EQ ("0", validate_text2);
}

TEST (rpc, successors)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
    rai::rpc rpc (system.service, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "successors");
	request.put ("block", genesis.to_string ());
	request.put ("count", std::to_string (std::numeric_limits <uint64_t>::max ()));
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
    auto & blocks_node (response.json.get_child ("blocks"));
	std::vector <rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (genesis, blocks [0]);
	ASSERT_EQ (block->hash(), blocks [1]);
}

TEST (rpc, bootstrap_any)
{
    rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto latest (system1.nodes [0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, rai::genesis_account, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system1.nodes [0]->generate_work (latest));
	{
		rai::transaction transaction (system1.nodes [0]->store.environment, nullptr, true);
		ASSERT_EQ (rai::process_result::progress, system1.nodes [0]->ledger.process (transaction, send).code);
	}
    rai::rpc rpc (system0.service, *system0.nodes [0], rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
	request.put ("action", "bootstrap_any");
	test_response response (request, rpc, system0.service);
	while (response.status == 0)
	{
		system0.poll ();
	}
	std::string success (response.json.get <std::string> ("success"));
	ASSERT_TRUE (success.empty());
}

TEST (rpc, republish)
{
    rai::system system (24000, 2);
	rai::keypair key;
	auto latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
	system.nodes [0]->process (send);
    rai::rpc rpc (system.service, node1, rai::rpc_config (true));
	rpc.start ();
    boost::property_tree::ptree request;
    request.put ("action", "republish");
	request.put ("hash", send.hash ().to_string ());
	test_response response (request, rpc, system.service);
	while (response.status == 0)
	{
		system.poll ();
	}
    ASSERT_EQ (200, response.status);
	auto iterations (0);
	while (system.nodes[1]->balance (rai::test_genesis_key.pub) == rai::genesis_amount)
	{
		system.poll ();
		++iterations;
		ASSERT_GT (200, iterations);
	}
}
