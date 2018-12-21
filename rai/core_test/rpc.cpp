#include <gtest/gtest.h>

#include <boost/algorithm/string.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/thread.hpp>
#include <rai/core_test/testutil.hpp>
#include <rai/node/common.hpp>
#include <rai/node/rpc.hpp>
#include <rai/node/testing.hpp>

using namespace std::chrono_literals;

class test_response
{
public:
	test_response (boost::property_tree::ptree const & request_a, rai::rpc & rpc_a, boost::asio::io_context & io_ctx) :
	request (request_a),
	sock (io_ctx),
	status (0)
	{
		sock.async_connect (rai::tcp_endpoint (boost::asio::ip::address_v6::loopback (), rpc_a.config.port), [this](boost::system::error_code const & ec) {
			if (!ec)
			{
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, request);
				req.method (boost::beast::http::verb::post);
				req.target ("/");
				req.version (11);
				ostream.flush ();
				req.body () = ostream.str ();
				req.prepare_payload ();
				boost::beast::http::async_write (sock, req, [this](boost::system::error_code const & ec, size_t bytes_transferred) {
					if (!ec)
					{
						boost::beast::http::async_read (sock, sb, resp, [this](boost::system::error_code const & ec, size_t bytes_transferred) {
							if (!ec)
							{
								std::stringstream body (resp.body ());
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
	boost::beast::flat_buffer sb;
	boost::beast::http::request<boost::beast::http::string_body> req;
	boost::beast::http::response<boost::beast::http::string_body> resp;
	int status;
};

TEST (rpc, account_balance)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_balance");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string balance_text (response.json.get<std::string> ("balance"));
	ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
	std::string pending_text (response.json.get<std::string> ("pending"));
	ASSERT_EQ ("0", pending_text);
}

TEST (rpc, account_block_count)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_block_count");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string block_count_text (response.json.get<std::string> ("block_count"));
	ASSERT_EQ ("1", block_count_text);
}

TEST (rpc, account_create)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_create");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto account_text (response.json.get<std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (system.wallet (0)->exists (account));
}

TEST (rpc, account_weight)
{
	rai::keypair key;
	rai::system system (24000, 1);
	rai::block_hash latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::change_block block (latest, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	ASSERT_EQ (rai::process_result::progress, node1.process (block).code);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_weight");
	request.put ("account", key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string balance_text (response.json.get<std::string> ("weight"));
	ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, wallet_contains)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_contains");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string exists_text (response.json.get<std::string> ("exists"));
	ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_doesnt_contain)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_contains");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string exists_text (response.json.get<std::string> ("exists"));
	ASSERT_EQ ("0", exists_text);
}

TEST (rpc, validate_account_number)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "validate_account_number");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::string exists_text (response.json.get<std::string> ("valid"));
	ASSERT_EQ ("1", exists_text);
}

TEST (rpc, validate_account_invalid)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	std::string account;
	rai::test_genesis_key.pub.encode_account (account);
	account[0] ^= 0x1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "validate_account_number");
	request.put ("account", account);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string exists_text (response.json.get<std::string> ("valid"));
	ASSERT_EQ ("0", exists_text);
}

TEST (rpc, send)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
	request.put ("destination", rai::test_genesis_key.pub.to_account ());
	request.put ("amount", "100");
	boost::thread thread2 ([&system]() {
		system.deadline_set (10s);
		while (system.nodes[0]->balance (rai::test_genesis_key.pub) == rai::genesis_amount)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	});
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string block_text (response.json.get<std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (system.nodes[0]->ledger.block_exists (block));
	ASSERT_EQ (system.nodes[0]->latest (rai::test_genesis_key.pub), block);
	thread2.join ();
}

TEST (rpc, send_fail)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
	request.put ("destination", rai::test_genesis_key.pub.to_account ());
	request.put ("amount", "100");
	std::atomic<bool> done (false);
	boost::thread thread2 ([&system, &done]() {
		system.deadline_set (10s);
		while (!done)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	});
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	done = true;
	ASSERT_EQ (response.json.get<std::string> ("error"), "Error generating block");
	thread2.join ();
}

TEST (rpc, send_work)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
	request.put ("destination", rai::test_genesis_key.pub.to_account ());
	request.put ("amount", "100");
	request.put ("work", "1");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (10s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (response.json.get<std::string> ("error"), "Invalid work");
	request.erase ("work");
	request.put ("work", rai::to_string_hex (system.nodes[0]->work_generate_blocking (system.nodes[0]->latest (rai::test_genesis_key.pub))));
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (10s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	std::string block_text (response2.json.get<std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (system.nodes[0]->ledger.block_exists (block));
	ASSERT_EQ (system.nodes[0]->latest (rai::test_genesis_key.pub), block);
}

TEST (rpc, send_idempotent)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "send");
	request.put ("source", rai::test_genesis_key.pub.to_account ());
	request.put ("destination", rai::account (0).to_account ());
	request.put ("amount", (rai::genesis_amount - (rai::genesis_amount / 4)).convert_to<std::string> ());
	request.put ("id", "123abc");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string block_text (response.json.get<std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (system.nodes[0]->ledger.block_exists (block));
	ASSERT_EQ (system.nodes[0]->balance (rai::test_genesis_key.pub), rai::genesis_amount / 4);
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	ASSERT_EQ ("", response2.json.get<std::string> ("error", ""));
	ASSERT_EQ (block_text, response2.json.get<std::string> ("block"));
	ASSERT_EQ (system.nodes[0]->balance (rai::test_genesis_key.pub), rai::genesis_amount / 4);
	request.erase ("id");
	request.put ("id", "456def");
	test_response response3 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response3.status);
	ASSERT_EQ (response3.json.get<std::string> ("error"), "Insufficient balance");
}

TEST (rpc, stop)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "stop");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	};
	ASSERT_FALSE (system.nodes[0]->network.on);
}

TEST (rpc, wallet_add)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	rai::keypair key1;
	std::string key_text;
	key1.prv.data.encode_hex (key_text);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_add");
	request.put ("key", key_text);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("account"));
	ASSERT_EQ (account_text1, key1.pub.to_account ());
	ASSERT_TRUE (system.wallet (0)->exists (key1.pub));
}

TEST (rpc, wallet_password_valid)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "password_valid");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("valid"));
	ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_password_change)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "password_change");
	request.put ("password", "test");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("changed"));
	ASSERT_EQ (account_text1, "1");
	auto transaction (system.wallet (0)->wallets.tx_begin (true));
	ASSERT_TRUE (system.wallet (0)->store.valid_password (transaction));
	ASSERT_TRUE (system.wallet (0)->enter_password (transaction, ""));
	ASSERT_FALSE (system.wallet (0)->store.valid_password (transaction));
	ASSERT_FALSE (system.wallet (0)->enter_password (transaction, "test"));
	ASSERT_TRUE (system.wallet (0)->store.valid_password (transaction));
}

TEST (rpc, wallet_password_enter)
{
	rai::system system (24000, 1);
	rai::raw_key password_l;
	password_l.data.clear ();
	system.deadline_set (10s);
	while (password_l.data == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
		system.wallet (0)->store.password.value (password_l);
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "password_enter");
	request.put ("password", "");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("valid"));
	ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_representative)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_representative");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("representative"));
	ASSERT_EQ (account_text1, rai::genesis_account.to_account ());
}

TEST (rpc, wallet_representative_set)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	rai::keypair key;
	request.put ("action", "wallet_representative_set");
	request.put ("representative", key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto transaction (system.nodes[0]->wallets.tx_begin ());
	ASSERT_EQ (key.pub, system.nodes[0]->wallets.items.begin ()->second->store.representative (transaction));
}

TEST (rpc, account_list)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	rai::keypair key2;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "account_list");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & accounts_node (response.json.get_child ("accounts"));
	std::vector<rai::uint256_union> accounts;
	for (auto i (accounts_node.begin ()), j (accounts_node.end ()); i != j; ++i)
	{
		auto account (i->second.get<std::string> (""));
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
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_key_valid");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string exists_text (response.json.get<std::string> ("valid"));
	ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_create)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_create");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string wallet_text (response.json.get<std::string> ("wallet"));
	rai::uint256_union wallet_id;
	ASSERT_FALSE (wallet_id.decode_hex (wallet_text));
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.find (wallet_id));
}

TEST (rpc, wallet_export)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "wallet_export");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string wallet_json (response.json.get<std::string> ("json"));
	bool error (false);
	auto transaction (system.nodes[0]->wallets.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store store (error, kdf, transaction, rai::genesis_account, 1, "0", wallet_json);
	ASSERT_FALSE (error);
	ASSERT_TRUE (store.exists (transaction, rai::test_genesis_key.pub));
}

TEST (rpc, wallet_destroy)
{
	rai::system system (24000, 1);
	auto wallet_id (system.nodes[0]->wallets.items.begin ()->first);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "wallet_destroy");
	request.put ("wallet", wallet_id.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.find (wallet_id));
}

TEST (rpc, account_move)
{
	rai::system system (24000, 1);
	auto wallet_id (system.nodes[0]->wallets.items.begin ()->first);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	auto destination (system.wallet (0));
	rai::keypair key;
	destination->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair source_id;
	auto source (system.nodes[0]->wallets.create (source_id.pub));
	source->insert_adhoc (key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "account_move");
	request.put ("wallet", wallet_id.to_string ());
	request.put ("source", source_id.pub.to_string ());
	boost::property_tree::ptree keys;
	boost::property_tree::ptree entry;
	entry.put ("", key.pub.to_account ());
	keys.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", keys);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ ("1", response.json.get<std::string> ("moved"));
	ASSERT_TRUE (destination->exists (key.pub));
	ASSERT_TRUE (destination->exists (rai::test_genesis_key.pub));
	auto transaction (system.nodes[0]->wallets.tx_begin ());
	ASSERT_EQ (source->store.end (), source->store.begin (transaction));
}

TEST (rpc, block)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block");
	request.put ("hash", system.nodes[0]->latest (rai::genesis_account).to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto contents (response.json.get<std::string> ("contents"));
	ASSERT_FALSE (contents.empty ());
}

TEST (rpc, block_account)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	rai::genesis genesis;
	boost::property_tree::ptree request;
	request.put ("action", "block_account");
	request.put ("hash", genesis.hash ().to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text (response.json.get<std::string> ("account"));
	rai::account account;
	ASSERT_FALSE (account.decode_account (account_text));
}

TEST (rpc, chain)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes[0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "chain");
	request.put ("block", block->hash ().to_string ());
	request.put ("count", std::to_string (std::numeric_limits<uint64_t>::max ()));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & blocks_node (response.json.get_child ("blocks"));
	std::vector<rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block->hash (), blocks[0]);
	ASSERT_EQ (genesis, blocks[1]);
}

TEST (rpc, chain_limit)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes[0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "chain");
	request.put ("block", block->hash ().to_string ());
	request.put ("count", 1);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & blocks_node (response.json.get_child ("blocks"));
	std::vector<rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block->hash (), blocks[0]);
}

TEST (rpc, frontier)
{
	rai::system system (24000, 1);
	std::unordered_map<rai::account, rai::block_hash> source;
	{
		auto transaction (system.nodes[0]->wallets.tx_begin (true));
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source[key.pub] = key.prv.data;
			system.nodes[0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0, rai::epoch::epoch_0));
		}
	}
	rai::keypair key;
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (std::numeric_limits<uint64_t>::max ()));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & frontiers_node (response.json.get_child ("frontiers"));
	std::unordered_map<rai::account, rai::block_hash> frontiers;
	for (auto i (frontiers_node.begin ()), j (frontiers_node.end ()); i != j; ++i)
	{
		rai::account account;
		account.decode_account (i->first);
		rai::block_hash frontier;
		frontier.decode_hex (i->second.get<std::string> (""));
		frontiers[account] = frontier;
	}
	ASSERT_EQ (1, frontiers.erase (rai::test_genesis_key.pub));
	ASSERT_EQ (source, frontiers);
}

TEST (rpc, frontier_limited)
{
	rai::system system (24000, 1);
	std::unordered_map<rai::account, rai::block_hash> source;
	{
		auto transaction (system.nodes[0]->wallets.tx_begin (true));
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source[key.pub] = key.prv.data;
			system.nodes[0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0, rai::epoch::epoch_0));
		}
	}
	rai::keypair key;
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (100));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & frontiers_node (response.json.get_child ("frontiers"));
	ASSERT_EQ (100, frontiers_node.size ());
}

TEST (rpc, frontier_startpoint)
{
	rai::system system (24000, 1);
	std::unordered_map<rai::account, rai::block_hash> source;
	{
		auto transaction (system.nodes[0]->wallets.tx_begin (true));
		for (auto i (0); i < 1000; ++i)
		{
			rai::keypair key;
			source[key.pub] = key.prv.data;
			system.nodes[0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0, 0, rai::epoch::epoch_0));
		}
	}
	rai::keypair key;
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "frontiers");
	request.put ("account", source.begin ()->first.to_account ());
	request.put ("count", std::to_string (1));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
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
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (0)->receive_action (*send, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
	auto node0 (system.nodes[0]);
	rai::genesis genesis;
	rai::state_block usend (rai::genesis_account, node0->latest (rai::genesis_account), rai::genesis_account, rai::genesis_amount - rai::Gxrb_ratio, rai::genesis_account, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::state_block ureceive (rai::genesis_account, usend.hash (), rai::genesis_account, rai::genesis_amount, usend.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::state_block uchange (rai::genesis_account, ureceive.hash (), rai::keypair ().pub, rai::genesis_amount, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	{
		auto transaction (node0->wallets.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction, usend).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction, ureceive).code);
		ASSERT_EQ (rai::process_result::progress, node0->ledger.process (transaction, uchange).code);
	}
	rai::rpc rpc (system.io_ctx, *node0, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "history");
	request.put ("hash", uchange.hash ().to_string ());
	request.put ("count", 100);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::vector<std::tuple<std::string, std::string, std::string, std::string>> history_l;
	auto & history_node (response.json.get_child ("history"));
	for (auto i (history_node.begin ()), n (history_node.end ()); i != n; ++i)
	{
		history_l.push_back (std::make_tuple (i->second.get<std::string> ("type"), i->second.get<std::string> ("account"), i->second.get<std::string> ("amount"), i->second.get<std::string> ("hash")));
	}
	ASSERT_EQ (5, history_l.size ());
	ASSERT_EQ ("receive", std::get<0> (history_l[0]));
	ASSERT_EQ (ureceive.hash ().to_string (), std::get<3> (history_l[0]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get<1> (history_l[0]));
	ASSERT_EQ (rai::Gxrb_ratio.convert_to<std::string> (), std::get<2> (history_l[0]));
	ASSERT_EQ (5, history_l.size ());
	ASSERT_EQ ("send", std::get<0> (history_l[1]));
	ASSERT_EQ (usend.hash ().to_string (), std::get<3> (history_l[1]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get<1> (history_l[1]));
	ASSERT_EQ (rai::Gxrb_ratio.convert_to<std::string> (), std::get<2> (history_l[1]));
	ASSERT_EQ ("receive", std::get<0> (history_l[2]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get<1> (history_l[2]));
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.to_string_dec (), std::get<2> (history_l[2]));
	ASSERT_EQ (receive->hash ().to_string (), std::get<3> (history_l[2]));
	ASSERT_EQ ("send", std::get<0> (history_l[3]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get<1> (history_l[3]));
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.to_string_dec (), std::get<2> (history_l[3]));
	ASSERT_EQ (send->hash ().to_string (), std::get<3> (history_l[3]));
	ASSERT_EQ ("receive", std::get<0> (history_l[4]));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), std::get<1> (history_l[4]));
	ASSERT_EQ (rai::genesis_amount.convert_to<std::string> (), std::get<2> (history_l[4]));
	ASSERT_EQ (genesis.hash ().to_string (), std::get<3> (history_l[4]));
}

TEST (rpc, history_count)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto change (system.wallet (0)->change_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub));
	ASSERT_NE (nullptr, change);
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (0)->receive_action (*send, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "history");
	request.put ("hash", receive->hash ().to_string ());
	request.put ("count", 1);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & history_node (response.json.get_child ("history"));
	ASSERT_EQ (1, history_node.size ());
}

TEST (rpc, process_block)
{
	rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (system.nodes[0]->latest (rai::test_genesis_key.pub) != send.hash ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::string send_hash (response.json.get<std::string> ("hash"));
	ASSERT_EQ (send.hash ().to_string (), send_hash);
}

TEST (rpc, process_block_no_work)
{
	rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	send.block_work_set (0);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_FALSE (response.json.get<std::string> ("error", "").empty ());
}

TEST (rpc, process_republish)
{
	rai::system system (24000, 2);
	rai::keypair key;
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (system.nodes[1]->latest (rai::test_genesis_key.pub) != send.hash ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (rpc, keepalive)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (std::make_shared<rai::node> (init1, system.io_ctx, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
	node1->start ();
	system.nodes.push_back (node1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "keepalive");
	auto address (boost::str (boost::format ("%1%") % node1->network.endpoint ().address ()));
	auto port (boost::str (boost::format ("%1%") % node1->network.endpoint ().port ()));
	request.put ("address", address);
	request.put ("port", port);
	ASSERT_FALSE (system.nodes[0]->peers.known_peer (node1->network.endpoint ()));
	ASSERT_EQ (0, system.nodes[0]->peers.size ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (!system.nodes[0]->peers.known_peer (node1->network.endpoint ()))
	{
		ASSERT_EQ (0, system.nodes[0]->peers.size ());
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (rpc, payment_init)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "payment_init");
	request.put ("wallet", wallet_id.pub.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ ("Ready", response.json.get<std::string> ("status"));
}

TEST (rpc, payment_begin_end)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get<std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	rai::block_hash root1;
	{
		auto transaction (node1->store.tx_begin ());
		root1 = node1->ledger.latest_root (transaction, account);
	}
	uint64_t work (0);
	while (!rai::work_validate (root1, work))
	{
		++work;
		ASSERT_LT (work, 50);
	}
	system.deadline_set (10s);
	while (rai::work_validate (root1, work))
	{
		auto ec = system.poll ();
		auto transaction (wallet->wallets.tx_begin ());
		ASSERT_FALSE (wallet->store.work_get (transaction, account, work));
		ASSERT_NO_ERROR (ec);
	}
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	test_response response2 (request2, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
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
	auto node1 (system.nodes[0]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto transaction (node1->store.tx_begin ());
	system.wallet (0)->init_free_accounts (transaction);
	auto wallet_id (node1->wallets.items.begin ()->first);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_end");
	request1.put ("wallet", wallet_id.to_string ());
	request1.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_FALSE (response1.json.get<std::string> ("error", "").empty ());
}

TEST (rpc, payment_zero_balance)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto transaction (node1->store.tx_begin ());
	system.wallet (0)->init_free_accounts (transaction);
	auto wallet_id (node1->wallets.items.begin ()->first);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.to_string ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get<std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_NE (rai::test_genesis_key.pub, account);
}

TEST (rpc, payment_begin_reuse)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	auto account_text (response1.json.get<std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	test_response response2 (request2, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_NE (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	test_response response3 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response3.status);
	auto account2_text (response1.json.get<std::string> ("account"));
	rai::uint256_union account2;
	ASSERT_FALSE (account2.decode_account (account2_text));
	ASSERT_EQ (account, account2);
}

TEST (rpc, payment_begin_locked)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	{
		auto transaction (wallet->wallets.tx_begin (true));
		wallet->store.rekey (transaction, "1");
		ASSERT_TRUE (wallet->store.attempt_password (transaction, ""));
	}
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_FALSE (response1.json.get<std::string> ("error", "").empty ());
}

TEST (rpc, payment_wait)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "payment_wait");
	request1.put ("account", key.pub.to_account ());
	request1.put ("amount", rai::amount (rai::Mxrb_ratio).to_string_dec ());
	request1.put ("timeout", "100");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("nothing", response1.json.get<std::string> ("status"));
	request1.put ("timeout", "100000");
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mxrb_ratio);
	system.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (500), [&]() {
		system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mxrb_ratio);
	});
	test_response response2 (request1, rpc, system.io_ctx);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	ASSERT_EQ ("success", response2.json.get<std::string> ("status"));
	request1.put ("amount", rai::amount (rai::Mxrb_ratio * 2).to_string_dec ());
	test_response response3 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response3.status);
	ASSERT_EQ ("success", response2.json.get<std::string> ("status"));
}

TEST (rpc, peers)
{
	rai::system system (24000, 2);
	system.nodes[0]->peers.insert (rai::endpoint (boost::asio::ip::address_v6::from_string ("::ffff:80.80.80.80"), 4000), rai::protocol_version);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "peers");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & peers_node (response.json.get_child ("peers"));
	ASSERT_EQ (2, peers_node.size ());
}

TEST (rpc, pending)
{
	rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));
	system.deadline_set (5s);
	while (system.nodes[0]->active.active (*block1))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "pending");
	request.put ("account", key1.pub.to_account ());
	request.put ("count", "100");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & blocks_node (response.json.get_child ("blocks"));
	ASSERT_EQ (1, blocks_node.size ());
	rai::block_hash hash1 (blocks_node.begin ()->second.get<std::string> (""));
	ASSERT_EQ (block1->hash (), hash1);
	request.put ("threshold", "100"); // Threshold test
	test_response response0 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response0.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response0.status);
	blocks_node = response0.json.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	std::unordered_map<rai::block_hash, rai::uint128_union> blocks;
	for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
	{
		rai::block_hash hash;
		hash.decode_hex (i->first);
		rai::uint128_union amount;
		amount.decode_dec (i->second.get<std::string> (""));
		blocks[hash] = amount;
		boost::optional<std::string> source (i->second.get_optional<std::string> ("source"));
		ASSERT_FALSE (source.is_initialized ());
		boost::optional<uint8_t> min_version (i->second.get_optional<uint8_t> ("min_version"));
		ASSERT_FALSE (min_version.is_initialized ());
	}
	ASSERT_EQ (blocks[block1->hash ()], 100);
	request.put ("threshold", "101");
	test_response response1 (request, rpc, system.io_ctx);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	blocks_node = response1.json.get_child ("blocks");
	ASSERT_EQ (0, blocks_node.size ());
	request.put ("threshold", "0");
	request.put ("source", "true");
	request.put ("min_version", "true");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	blocks_node = response2.json.get_child ("blocks");
	ASSERT_EQ (1, blocks_node.size ());
	std::unordered_map<rai::block_hash, rai::uint128_union> amounts;
	std::unordered_map<rai::block_hash, rai::account> sources;
	for (auto i (blocks_node.begin ()), j (blocks_node.end ()); i != j; ++i)
	{
		rai::block_hash hash;
		hash.decode_hex (i->first);
		amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
		sources[hash].decode_account (i->second.get<std::string> ("source"));
		ASSERT_EQ (i->second.get<uint8_t> ("min_version"), 0);
	}
	ASSERT_EQ (amounts[block1->hash ()], 100);
	ASSERT_EQ (sources[block1->hash ()], rai::test_genesis_key.pub);
}

TEST (rpc_config, serialization)
{
	rai::rpc_config config1;
	config1.address = boost::asio::ip::address_v6::any ();
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
	auto wallet (system.nodes[0]->wallets.items.begin ()->first.to_string ());
	rai::send_block block (system.nodes[0]->latest (rai::test_genesis_key.pub), rai::test_genesis_key.pub, rai::genesis_amount - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	auto transaction (system.nodes[0]->store.tx_begin (true));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->ledger.process (transaction, block).code);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "search_pending");
	request.put ("wallet", wallet);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (system.nodes[0]->balance (rai::test_genesis_key.pub) != rai::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (rpc, version)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "version");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("rpc_version"));
	ASSERT_EQ (200, response1.status);
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		ASSERT_EQ (std::to_string (node1->store.version_get (transaction)), response1.json.get<std::string> ("store_version"));
	}
	ASSERT_EQ (std::to_string (rai::protocol_version), response1.json.get<std::string> ("protocol_version"));
	ASSERT_EQ (boost::str (boost::format ("RaiBlocks %1%.%2%") % RAIBLOCKS_VERSION_MAJOR % RAIBLOCKS_VERSION_MINOR), response1.json.get<std::string> ("node_vendor"));
	auto headers (response1.resp.base ());
	auto allowed_origin (headers.at ("Access-Control-Allow-Origin"));
	auto allowed_headers (headers.at ("Access-Control-Allow-Headers"));
	ASSERT_EQ ("*", allowed_origin);
	ASSERT_EQ ("Accept, Accept-Language, Content-Language, Content-Type", allowed_headers);
}

TEST (rpc, work_generate)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto node1 (system.nodes[0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash1 (1);
	boost::property_tree::ptree request1;
	request1.put ("action", "work_generate");
	request1.put ("hash", hash1.to_string ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	auto work1 (response1.json.get<std::string> ("work"));
	uint64_t work2;
	ASSERT_FALSE (rai::from_string_hex (work1, work2));
	ASSERT_FALSE (rai::work_validate (hash1, work2));
}

TEST (rpc, work_cancel)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash1 (1);
	boost::property_tree::ptree request1;
	request1.put ("action", "work_cancel");
	request1.put ("hash", hash1.to_string ());
	auto done (false);
	system.deadline_set (10s);
	while (!done)
	{
		system.work.generate (hash1, [&done](boost::optional<uint64_t> work_a) {
			done = !work_a;
		});
		test_response response1 (request1, rpc, system.io_ctx);
		std::error_code ec;
		while (response1.status == 0)
		{
			ec = system.poll ();
		}
		ASSERT_EQ (200, response1.status);
		ASSERT_NO_ERROR (ec);
	}
}

TEST (rpc, work_peer_bad)
{
	rai::system system (24000, 2);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	node2.config.work_peers.push_back (std::make_pair (boost::asio::ip::address_v6::any ().to_string (), 0));
	rai::block_hash hash1 (1);
	std::atomic<uint64_t> work (0);
	node2.work_generate (hash1, [&work](uint64_t work_a) {
		work = work_a;
	});
	system.deadline_set (5s);
	while (rai::work_validate (hash1, work))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (rpc, work_peer_one)
{
	rai::system system (24000, 2);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	node2.config.work_peers.push_back (std::make_pair (node1.network.endpoint ().address ().to_string (), rpc.config.port));
	rai::keypair key1;
	uint64_t work (0);
	node2.work_generate (key1.pub, [&work](uint64_t work_a) {
		work = work_a;
	});
	system.deadline_set (5s);
	while (rai::work_validate (key1.pub, work))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (rpc, work_peer_many)
{
	rai::system system1 (24000, 1);
	rai::system system2 (24001, 1);
	rai::system system3 (24002, 1);
	rai::system system4 (24003, 1);
	rai::node_init init1;
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	auto & node3 (*system3.nodes[0]);
	auto & node4 (*system4.nodes[0]);
	rai::keypair key;
	rai::rpc_config config2 (true);
	config2.port += 0;
	rai::rpc rpc2 (system2.io_ctx, node2, config2);
	rpc2.start ();
	rai::rpc_config config3 (true);
	config3.port += 1;
	rai::rpc rpc3 (system3.io_ctx, node3, config3);
	rpc3.start ();
	rai::rpc_config config4 (true);
	config4.port += 2;
	rai::rpc rpc4 (system4.io_ctx, node4, config4);
	rpc4.start ();
	node1.config.work_peers.push_back (std::make_pair (node2.network.endpoint ().address ().to_string (), rpc2.config.port));
	node1.config.work_peers.push_back (std::make_pair (node3.network.endpoint ().address ().to_string (), rpc3.config.port));
	node1.config.work_peers.push_back (std::make_pair (node4.network.endpoint ().address ().to_string (), rpc4.config.port));
	for (auto i (0); i < 10; ++i)
	{
		rai::keypair key1;
		uint64_t work (0);
		node1.work_generate (key1.pub, [&work](uint64_t work_a) {
			work = work_a;
		});
		while (rai::work_validate (key1.pub, work))
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
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "block_count");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("count"));
	ASSERT_EQ ("0", response1.json.get<std::string> ("unchecked"));
}

TEST (rpc, frontier_count)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "frontier_count");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("count"));
}

TEST (rpc, account_count)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "account_count");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("count"));
}

TEST (rpc, available_supply)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "available_supply");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("0", response1.json.get<std::string> ("available"));
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	test_response response2 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	ASSERT_EQ ("1", response2.json.get<std::string> ("available"));
	auto block2 (system.wallet (0)->send_action (rai::test_genesis_key.pub, 0, 100)); // Sending to burning 0 account
	test_response response3 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response3.status);
	ASSERT_EQ ("1", response3.json.get<std::string> ("available"));
}

TEST (rpc, mrai_to_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "mrai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::Mxrb_ratio.convert_to<std::string> (), response1.json.get<std::string> ("amount"));
}

TEST (rpc, mrai_from_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "mrai_from_raw");
	request1.put ("amount", rai::Mxrb_ratio.convert_to<std::string> ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("amount"));
}

TEST (rpc, krai_to_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "krai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::kxrb_ratio.convert_to<std::string> (), response1.json.get<std::string> ("amount"));
}

TEST (rpc, krai_from_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "krai_from_raw");
	request1.put ("amount", rai::kxrb_ratio.convert_to<std::string> ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("amount"));
}

TEST (rpc, rai_to_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "rai_to_raw");
	request1.put ("amount", "1");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ (rai::xrb_ratio.convert_to<std::string> (), response1.json.get<std::string> ("amount"));
}

TEST (rpc, rai_from_raw)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request1;
	request1.put ("action", "rai_from_raw");
	request1.put ("amount", rai::xrb_ratio.convert_to<std::string> ());
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	ASSERT_EQ ("1", response1.json.get<std::string> ("amount"));
}

TEST (rpc, account_representative)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	request.put ("account", rai::genesis_account.to_account ());
	request.put ("action", "account_representative");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("representative"));
	ASSERT_EQ (account_text1, rai::genesis_account.to_account ());
}

TEST (rpc, account_representative_set)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	rai::keypair rep;
	request.put ("account", rai::genesis_account.to_account ());
	request.put ("representative", rep.pub.to_account ());
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("action", "account_representative_set");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string block_text1 (response.json.get<std::string> ("block"));
	rai::block_hash hash;
	ASSERT_FALSE (hash.decode_hex (block_text1));
	ASSERT_FALSE (hash.is_zero ());
	auto transaction (system.nodes[0]->store.tx_begin ());
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction, hash));
	ASSERT_EQ (rep.pub, system.nodes[0]->store.block_get (transaction, hash)->representative ());
}

TEST (rpc, bootstrap)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto latest (system1.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, rai::genesis_account, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system1.nodes[0]->work_generate_blocking (latest));
	{
		auto transaction (system1.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system1.nodes[0]->ledger.process (transaction, send).code);
	}
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "bootstrap");
	request.put ("address", "::ffff:127.0.0.1");
	request.put ("port", system1.nodes[0]->network.endpoint ().port ());
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	system1.deadline_set (10s);
	while (system0.nodes[0]->latest (rai::genesis_account) != system1.nodes[0]->latest (rai::genesis_account))
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

TEST (rpc, account_remove)
{
	rai::system system0 (24000, 1);
	auto key1 (system0.wallet (0)->deterministic_insert ());
	ASSERT_TRUE (system0.wallet (0)->exists (key1));
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_remove");
	request.put ("wallet", system0.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", key1.to_account ());
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_FALSE (system0.wallet (0)->exists (key1));
}

TEST (rpc, representatives)
{
	rai::system system0 (24000, 1);
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "representatives");
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response.status);
	auto & representatives_node (response.json.get_child ("representatives"));
	std::vector<rai::account> representatives;
	for (auto i (representatives_node.begin ()), n (representatives_node.end ()); i != n; ++i)
	{
		rai::account account;
		ASSERT_FALSE (account.decode_account (i->first));
		representatives.push_back (account);
	}
	ASSERT_EQ (1, representatives.size ());
	ASSERT_EQ (rai::genesis_account, representatives[0]);
}

TEST (rpc, wallet_change_seed)
{
	rai::system system0 (24000, 1);
	rai::keypair seed;
	{
		auto transaction (system0.nodes[0]->store.tx_begin ());
		rai::raw_key seed0;
		system0.wallet (0)->store.seed (seed0, transaction);
		ASSERT_NE (seed.pub, seed0.data);
	}
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_change_seed");
	request.put ("wallet", system0.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("seed", seed.pub.to_string ());
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response.status);
	{
		auto transaction (system0.nodes[0]->store.tx_begin ());
		rai::raw_key seed0;
		system0.wallet (0)->store.seed (seed0, transaction);
		ASSERT_EQ (seed.pub, seed0.data);
	}
}

TEST (rpc, wallet_frontiers)
{
	rai::system system0 (24000, 1);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_frontiers");
	request.put ("wallet", system0.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response.status);
	auto & frontiers_node (response.json.get_child ("frontiers"));
	std::vector<rai::account> frontiers;
	for (auto i (frontiers_node.begin ()), n (frontiers_node.end ()); i != n; ++i)
	{
		frontiers.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (1, frontiers.size ());
	ASSERT_EQ (system0.nodes[0]->latest (rai::genesis_account), frontiers[0]);
}

TEST (rpc, work_validate)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	rai::block_hash hash (1);
	uint64_t work1 (node1.work_generate_blocking (hash));
	boost::property_tree::ptree request;
	request.put ("action", "work_validate");
	request.put ("hash", hash.to_string ());
	request.put ("work", rai::to_string_hex (work1));
	test_response response1 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	std::string validate_text1 (response1.json.get<std::string> ("valid"));
	ASSERT_EQ ("1", validate_text1);
	uint64_t work2 (0);
	request.put ("work", rai::to_string_hex (work2));
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	std::string validate_text2 (response2.json.get<std::string> ("valid"));
	ASSERT_EQ ("0", validate_text2);
}

TEST (rpc, successors)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes[0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "successors");
	request.put ("block", genesis.to_string ());
	request.put ("count", std::to_string (std::numeric_limits<uint64_t>::max ()));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & blocks_node (response.json.get_child ("blocks"));
	std::vector<rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (genesis, blocks[0]);
	ASSERT_EQ (block->hash (), blocks[1]);
}

TEST (rpc, bootstrap_any)
{
	rai::system system0 (24000, 1);
	rai::system system1 (24001, 1);
	auto latest (system1.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, rai::genesis_account, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system1.nodes[0]->work_generate_blocking (latest));
	{
		auto transaction (system1.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system1.nodes[0]->ledger.process (transaction, send).code);
	}
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "bootstrap_any");
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	std::string success (response.json.get<std::string> ("success"));
	ASSERT_TRUE (success.empty ());
}

TEST (rpc, republish)
{
	rai::system system (24000, 2);
	rai::keypair key;
	rai::genesis genesis;
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "republish");
	request.put ("hash", send.hash ().to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (system.nodes[1]->balance (rai::test_genesis_key.pub) == rai::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto & blocks_node (response.json.get_child ("blocks"));
	std::vector<rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (send.hash (), blocks[0]);

	request.put ("hash", genesis.hash ().to_string ());
	request.put ("count", 1);
	test_response response1 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	blocks_node = response1.json.get_child ("blocks");
	blocks.clear ();
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (genesis.hash (), blocks[0]);

	request.put ("hash", open.hash ().to_string ());
	request.put ("sources", 2);
	test_response response2 (request, rpc, system.io_ctx);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	blocks_node = response2.json.get_child ("blocks");
	blocks.clear ();
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (3, blocks.size ());
	ASSERT_EQ (genesis.hash (), blocks[0]);
	ASSERT_EQ (send.hash (), blocks[1]);
	ASSERT_EQ (open.hash (), blocks[2]);
}

TEST (rpc, deterministic_key)
{
	rai::system system0 (24000, 1);
	rai::raw_key seed;
	{
		auto transaction (system0.nodes[0]->store.tx_begin ());
		system0.wallet (0)->store.seed (seed, transaction);
	}
	rai::account account0 (system0.wallet (0)->deterministic_insert ());
	rai::account account1 (system0.wallet (0)->deterministic_insert ());
	rai::account account2 (system0.wallet (0)->deterministic_insert ());
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "deterministic_key");
	request.put ("seed", seed.data.to_string ());
	request.put ("index", "0");
	test_response response0 (request, rpc, system0.io_ctx);
	while (response0.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response0.status);
	std::string validate_text (response0.json.get<std::string> ("account"));
	ASSERT_EQ (account0.to_account (), validate_text);
	request.put ("index", "2");
	test_response response1 (request, rpc, system0.io_ctx);
	while (response1.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response1.status);
	validate_text = response1.json.get<std::string> ("account");
	ASSERT_NE (account1.to_account (), validate_text);
	ASSERT_EQ (account2.to_account (), validate_text);
}

TEST (rpc, accounts_balances)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "accounts_balances");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", rai::test_genesis_key.pub.to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	for (auto & balances : response.json.get_child ("balances"))
	{
		std::string account_text (balances.first);
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string balance_text (balances.second.get<std::string> ("balance"));
		ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
		std::string pending_text (balances.second.get<std::string> ("pending"));
		ASSERT_EQ ("0", pending_text);
	}
}

TEST (rpc, accounts_frontiers)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "accounts_frontiers");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", rai::test_genesis_key.pub.to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	for (auto & frontiers : response.json.get_child ("frontiers"))
	{
		std::string account_text (frontiers.first);
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string frontier_text (frontiers.second.get<std::string> (""));
		ASSERT_EQ (system.nodes[0]->latest (rai::genesis_account), frontier_text);
	}
}

TEST (rpc, accounts_pending)
{
	rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));
	auto iterations (0);
	system.deadline_set (5s);
	while (system.nodes[0]->active.active (*block1))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "accounts_pending");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", key1.pub.to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	request.put ("count", "100");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	for (auto & blocks : response.json.get_child ("blocks"))
	{
		std::string account_text (blocks.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		rai::block_hash hash1 (blocks.second.begin ()->second.get<std::string> (""));
		ASSERT_EQ (block1->hash (), hash1);
	}
	request.put ("threshold", "100"); // Threshold test
	test_response response1 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	std::unordered_map<rai::block_hash, rai::uint128_union> blocks;
	for (auto & pending : response1.json.get_child ("blocks"))
	{
		std::string account_text (pending.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			rai::block_hash hash;
			hash.decode_hex (i->first);
			rai::uint128_union amount;
			amount.decode_dec (i->second.get<std::string> (""));
			blocks[hash] = amount;
			boost::optional<std::string> source (i->second.get_optional<std::string> ("source"));
			ASSERT_FALSE (source.is_initialized ());
		}
	}
	ASSERT_EQ (blocks[block1->hash ()], 100);
	request.put ("source", "true");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	std::unordered_map<rai::block_hash, rai::uint128_union> amounts;
	std::unordered_map<rai::block_hash, rai::account> sources;
	for (auto & pending : response2.json.get_child ("blocks"))
	{
		std::string account_text (pending.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			rai::block_hash hash;
			hash.decode_hex (i->first);
			amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
			sources[hash].decode_account (i->second.get<std::string> ("source"));
		}
	}
	ASSERT_EQ (amounts[block1->hash ()], 100);
	ASSERT_EQ (sources[block1->hash ()], rai::test_genesis_key.pub);
}

TEST (rpc, blocks)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "blocks");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", system.nodes[0]->latest (rai::genesis_account).to_string ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("hashes", peers_l);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	for (auto & blocks : response.json.get_child ("blocks"))
	{
		std::string hash_text (blocks.first);
		ASSERT_EQ (system.nodes[0]->latest (rai::genesis_account).to_string (), hash_text);
		std::string blocks_text (blocks.second.get<std::string> (""));
		ASSERT_FALSE (blocks_text.empty ());
	}
}

TEST (rpc, wallet_info)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (key.prv);
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	rai::account account (system.wallet (0)->deterministic_insert ());
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		system.wallet (0)->store.erase (transaction, account);
	}
	account = system.wallet (0)->deterministic_insert ();
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_info");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string balance_text (response.json.get<std::string> ("balance"));
	ASSERT_EQ ("340282366920938463463374607431768211454", balance_text);
	std::string pending_text (response.json.get<std::string> ("pending"));
	ASSERT_EQ ("1", pending_text);
	std::string count_text (response.json.get<std::string> ("accounts_count"));
	ASSERT_EQ ("3", count_text);
	std::string adhoc_count (response.json.get<std::string> ("adhoc_count"));
	ASSERT_EQ ("2", adhoc_count);
	std::string deterministic_count (response.json.get<std::string> ("deterministic_count"));
	ASSERT_EQ ("1", deterministic_count);
	std::string index_text (response.json.get<std::string> ("deterministic_index"));
	ASSERT_EQ ("2", index_text);
}

TEST (rpc, wallet_balances)
{
	rai::system system0 (24000, 1);
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_balances");
	request.put ("wallet", system0.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response.status);
	for (auto & balances : response.json.get_child ("balances"))
	{
		std::string account_text (balances.first);
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string balance_text (balances.second.get<std::string> ("balance"));
		ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
		std::string pending_text (balances.second.get<std::string> ("pending"));
		ASSERT_EQ ("0", pending_text);
	}
	rai::keypair key;
	system0.wallet (0)->insert_adhoc (key.prv);
	auto send (system0.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	request.put ("threshold", "2");
	test_response response1 (request, rpc, system0.io_ctx);
	while (response1.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response1.status);
	for (auto & balances : response1.json.get_child ("balances"))
	{
		std::string account_text (balances.first);
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string balance_text (balances.second.get<std::string> ("balance"));
		ASSERT_EQ ("340282366920938463463374607431768211454", balance_text);
		std::string pending_text (balances.second.get<std::string> ("pending"));
		ASSERT_EQ ("0", pending_text);
	}
}

TEST (rpc, pending_exists)
{
	rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto hash0 (system.nodes[0]->latest (rai::genesis_account));
	auto block1 (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));
	system.deadline_set (5s);
	while (system.nodes[0]->active.active (*block1))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "pending_exists");
	request.put ("hash", hash0.to_string ());
	test_response response0 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response0.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response0.status);
	std::string exists_text (response0.json.get<std::string> ("exists"));
	ASSERT_EQ ("0", exists_text);
	request.put ("hash", block1->hash ().to_string ());
	test_response response1 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	std::string exists_text1 (response1.json.get<std::string> ("exists"));
	ASSERT_EQ ("1", exists_text1);
}

TEST (rpc, wallet_pending)
{
	rai::system system0 (24000, 1);
	rai::keypair key1;
	system0.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system0.wallet (0)->insert_adhoc (key1.prv);
	auto block1 (system0.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 100));
	auto iterations (0);
	while (system0.nodes[0]->active.active (*block1))
	{
		system0.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	rai::rpc rpc (system0.io_ctx, *system0.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_pending");
	request.put ("wallet", system0.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("count", "100");
	test_response response (request, rpc, system0.io_ctx);
	while (response.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ (1, response.json.get_child ("blocks").size ());
	for (auto & pending : response.json.get_child ("blocks"))
	{
		std::string account_text (pending.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		rai::block_hash hash1 (pending.second.begin ()->second.get<std::string> (""));
		ASSERT_EQ (block1->hash (), hash1);
	}
	request.put ("threshold", "100"); // Threshold test
	test_response response0 (request, rpc, system0.io_ctx);
	while (response0.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response0.status);
	std::unordered_map<rai::block_hash, rai::uint128_union> blocks;
	ASSERT_EQ (1, response0.json.get_child ("blocks").size ());
	for (auto & pending : response0.json.get_child ("blocks"))
	{
		std::string account_text (pending.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			rai::block_hash hash;
			hash.decode_hex (i->first);
			rai::uint128_union amount;
			amount.decode_dec (i->second.get<std::string> (""));
			blocks[hash] = amount;
			boost::optional<std::string> source (i->second.get_optional<std::string> ("source"));
			ASSERT_FALSE (source.is_initialized ());
			boost::optional<uint8_t> min_version (i->second.get_optional<uint8_t> ("min_version"));
			ASSERT_FALSE (min_version.is_initialized ());
		}
	}
	ASSERT_EQ (blocks[block1->hash ()], 100);
	request.put ("threshold", "101");
	test_response response1 (request, rpc, system0.io_ctx);
	while (response1.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response1.status);
	auto & pending1 (response1.json.get_child ("blocks"));
	ASSERT_EQ (0, pending1.size ());
	request.put ("threshold", "0");
	request.put ("source", "true");
	request.put ("min_version", "true");
	test_response response2 (request, rpc, system0.io_ctx);
	while (response2.status == 0)
	{
		system0.poll ();
	}
	ASSERT_EQ (200, response2.status);
	std::unordered_map<rai::block_hash, rai::uint128_union> amounts;
	std::unordered_map<rai::block_hash, rai::account> sources;
	ASSERT_EQ (1, response0.json.get_child ("blocks").size ());
	for (auto & pending : response2.json.get_child ("blocks"))
	{
		std::string account_text (pending.first);
		ASSERT_EQ (key1.pub.to_account (), account_text);
		for (auto i (pending.second.begin ()), j (pending.second.end ()); i != j; ++i)
		{
			rai::block_hash hash;
			hash.decode_hex (i->first);
			amounts[hash].decode_dec (i->second.get<std::string> ("amount"));
			sources[hash].decode_account (i->second.get<std::string> ("source"));
			ASSERT_EQ (i->second.get<uint8_t> ("min_version"), 0);
		}
	}
	ASSERT_EQ (amounts[block1->hash ()], 100);
	ASSERT_EQ (sources[block1->hash ()], rai::test_genesis_key.pub);
}

TEST (rpc, receive_minimum)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "receive_minimum");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string amount (response.json.get<std::string> ("amount"));
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.to_string_dec (), amount);
}

TEST (rpc, receive_minimum_set)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "receive_minimum_set");
	request.put ("amount", "100");
	ASSERT_NE (system.nodes[0]->config.receive_minimum.to_string_dec (), "100");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string success (response.json.get<std::string> ("success"));
	ASSERT_TRUE (success.empty ());
	ASSERT_EQ (system.nodes[0]->config.receive_minimum.to_string_dec (), "100");
}

TEST (rpc, work_get)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->work_cache_blocking (rai::test_genesis_key.pub, system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "work_get");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string work_text (response.json.get<std::string> ("work"));
	uint64_t work (1);
	auto transaction (system.nodes[0]->store.tx_begin ());
	system.nodes[0]->wallets.items.begin ()->second->store.work_get (transaction, rai::genesis_account, work);
	ASSERT_EQ (rai::to_string_hex (work), work_text);
}

TEST (rpc, wallet_work_get)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->work_cache_blocking (rai::test_genesis_key.pub, system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_work_get");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto transaction (system.nodes[0]->store.tx_begin ());
	for (auto & works : response.json.get_child ("works"))
	{
		std::string account_text (works.first);
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string work_text (works.second.get<std::string> (""));
		uint64_t work (1);
		system.nodes[0]->wallets.items.begin ()->second->store.work_get (transaction, rai::genesis_account, work);
		ASSERT_EQ (rai::to_string_hex (work), work_text);
	}
}

TEST (rpc, work_set)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	uint64_t work0 (100);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "work_set");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	request.put ("work", rai::to_string_hex (work0));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string success (response.json.get<std::string> ("success"));
	ASSERT_TRUE (success.empty ());
	uint64_t work1 (1);
	auto transaction (system.nodes[0]->store.tx_begin ());
	system.nodes[0]->wallets.items.begin ()->second->store.work_get (transaction, rai::genesis_account, work1);
	ASSERT_EQ (work1, work0);
}

TEST (rpc, search_pending_all)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::send_block block (system.nodes[0]->latest (rai::test_genesis_key.pub), rai::test_genesis_key.pub, rai::genesis_amount - system.nodes[0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	auto transaction (system.nodes[0]->store.tx_begin (true));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->ledger.process (transaction, block).code);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "search_pending_all");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	system.deadline_set (10s);
	while (system.nodes[0]->balance (rai::test_genesis_key.pub) != rai::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (rpc, wallet_republish)
{
	rai::system system (24000, 1);
	rai::genesis genesis;
	rai::keypair key;
	while (key.pub < rai::test_genesis_key.pub)
	{
		rai::keypair key1;
		key.pub = key1.pub;
		key.prv.data = key1.prv.data;
	}
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_republish");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("count", 1);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & blocks_node (response.json.get_child ("blocks"));
	std::vector<rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (send.hash (), blocks[0]);
	ASSERT_EQ (open.hash (), blocks[1]);
}

TEST (rpc, delegators)
{
	rai::system system (24000, 1);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "delegators");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & delegators_node (response.json.get_child ("delegators"));
	boost::property_tree::ptree delegators;
	for (auto i (delegators_node.begin ()), n (delegators_node.end ()); i != n; ++i)
	{
		delegators.put ((i->first), (i->second.get<std::string> ("")));
	}
	ASSERT_EQ (2, delegators.size ());
	ASSERT_EQ ("100", delegators.get<std::string> (rai::test_genesis_key.pub.to_account ()));
	ASSERT_EQ ("340282366920938463463374607431768211355", delegators.get<std::string> (key.pub.to_account ()));
}

TEST (rpc, delegators_count)
{
	rai::system system (24000, 1);
	rai::keypair key;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "delegators_count");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string count (response.json.get<std::string> ("count"));
	ASSERT_EQ ("2", count);
}

TEST (rpc, account_info)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	auto time (rai::seconds_since_epoch ());

	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "account_info");
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string frontier (response.json.get<std::string> ("frontier"));
	ASSERT_EQ (send.hash ().to_string (), frontier);
	std::string open_block (response.json.get<std::string> ("open_block"));
	ASSERT_EQ (genesis.hash ().to_string (), open_block);
	std::string representative_block (response.json.get<std::string> ("representative_block"));
	ASSERT_EQ (genesis.hash ().to_string (), representative_block);
	std::string balance (response.json.get<std::string> ("balance"));
	ASSERT_EQ ("100", balance);
	std::string modified_timestamp (response.json.get<std::string> ("modified_timestamp"));
	ASSERT_LT (std::abs ((long)time - stol (modified_timestamp)), 5);
	std::string block_count (response.json.get<std::string> ("block_count"));
	ASSERT_EQ ("2", block_count);
	ASSERT_EQ (0, response.json.get<uint8_t> ("account_version"));
	boost::optional<std::string> weight (response.json.get_optional<std::string> ("weight"));
	ASSERT_FALSE (weight.is_initialized ());
	boost::optional<std::string> pending (response.json.get_optional<std::string> ("pending"));
	ASSERT_FALSE (pending.is_initialized ());
	boost::optional<std::string> representative (response.json.get_optional<std::string> ("representative"));
	ASSERT_FALSE (representative.is_initialized ());
	// Test for optional values
	request.put ("weight", "true");
	request.put ("pending", "1");
	request.put ("representative", "1");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	std::string weight2 (response2.json.get<std::string> ("weight"));
	ASSERT_EQ ("100", weight2);
	std::string pending2 (response2.json.get<std::string> ("pending"));
	ASSERT_EQ ("0", pending2);
	std::string representative2 (response2.json.get<std::string> ("representative"));
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), representative2);
}

TEST (rpc, blocks_info)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "blocks_info");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", system.nodes[0]->latest (rai::genesis_account).to_string ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("hashes", peers_l);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	for (auto & blocks : response.json.get_child ("blocks"))
	{
		std::string hash_text (blocks.first);
		ASSERT_EQ (system.nodes[0]->latest (rai::genesis_account).to_string (), hash_text);
		std::string account_text (blocks.second.get<std::string> ("block_account"));
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), account_text);
		std::string amount_text (blocks.second.get<std::string> ("amount"));
		ASSERT_EQ (rai::genesis_amount.convert_to<std::string> (), amount_text);
		std::string blocks_text (blocks.second.get<std::string> ("contents"));
		ASSERT_FALSE (blocks_text.empty ());
		boost::optional<std::string> pending (blocks.second.get_optional<std::string> ("pending"));
		ASSERT_FALSE (pending.is_initialized ());
		boost::optional<std::string> source (blocks.second.get_optional<std::string> ("source_account"));
		ASSERT_FALSE (source.is_initialized ());
		boost::optional<std::string> balance (blocks.second.get_optional<std::string> ("balance"));
		ASSERT_FALSE (balance.is_initialized ());
	}
	// Test for optional values
	request.put ("source", "true");
	request.put ("pending", "1");
	request.put ("balance", "true");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	for (auto & blocks : response2.json.get_child ("blocks"))
	{
		std::string source (blocks.second.get<std::string> ("source_account"));
		ASSERT_EQ ("0", source);
		std::string pending (blocks.second.get<std::string> ("pending"));
		ASSERT_EQ ("0", pending);
		std::string balance_text (blocks.second.get<std::string> ("balance"));
		ASSERT_EQ (rai::genesis_amount.convert_to<std::string> (), balance_text);
	}
}

TEST (rpc, work_peers_all)
{
	rai::system system (24000, 1);
	rai::node_init init1;
	auto & node1 (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "work_peer_add");
	request.put ("address", "::1");
	request.put ("port", "0");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string success (response.json.get<std::string> ("success", ""));
	ASSERT_TRUE (success.empty ());
	boost::property_tree::ptree request1;
	request1.put ("action", "work_peers");
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	auto & peers_node (response1.json.get_child ("work_peers"));
	std::vector<std::string> peers;
	for (auto i (peers_node.begin ()), n (peers_node.end ()); i != n; ++i)
	{
		peers.push_back (i->second.get<std::string> (""));
	}
	ASSERT_EQ (1, peers.size ());
	ASSERT_EQ ("::1:0", peers[0]);
	boost::property_tree::ptree request2;
	request2.put ("action", "work_peers_clear");
	test_response response2 (request2, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	success = response2.json.get<std::string> ("success", "");
	ASSERT_TRUE (success.empty ());
	test_response response3 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response3.status);
	peers_node = response3.json.get_child ("work_peers");
	ASSERT_EQ (0, peers_node.size ());
}

TEST (rpc, block_count_type)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto send (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (0)->receive_action (*send, rai::test_genesis_key.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block_count_type");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string send_count (response.json.get<std::string> ("send"));
	ASSERT_EQ ("0", send_count);
	std::string receive_count (response.json.get<std::string> ("receive"));
	ASSERT_EQ ("0", receive_count);
	std::string open_count (response.json.get<std::string> ("open"));
	ASSERT_EQ ("1", open_count);
	std::string change_count (response.json.get<std::string> ("change"));
	ASSERT_EQ ("0", change_count);
	std::string state_count (response.json.get<std::string> ("state"));
	ASSERT_EQ ("2", state_count);
}

TEST (rpc, ledger)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	auto time (rai::seconds_since_epoch ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "ledger");
	request.put ("sorting", "1");
	request.put ("count", "1");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	for (auto & accounts : response.json.get_child ("accounts"))
	{
		std::string account_text (accounts.first);
		ASSERT_EQ (key.pub.to_account (), account_text);
		std::string frontier (accounts.second.get<std::string> ("frontier"));
		ASSERT_EQ (open.hash ().to_string (), frontier);
		std::string open_block (accounts.second.get<std::string> ("open_block"));
		ASSERT_EQ (open.hash ().to_string (), open_block);
		std::string representative_block (accounts.second.get<std::string> ("representative_block"));
		ASSERT_EQ (open.hash ().to_string (), representative_block);
		std::string balance_text (accounts.second.get<std::string> ("balance"));
		ASSERT_EQ ("340282366920938463463374607431768211355", balance_text);
		std::string modified_timestamp (accounts.second.get<std::string> ("modified_timestamp"));
		ASSERT_LT (std::abs ((long)time - stol (modified_timestamp)), 5);
		std::string block_count (accounts.second.get<std::string> ("block_count"));
		ASSERT_EQ ("1", block_count);
		boost::optional<std::string> weight (accounts.second.get_optional<std::string> ("weight"));
		ASSERT_FALSE (weight.is_initialized ());
		boost::optional<std::string> pending (accounts.second.get_optional<std::string> ("pending"));
		ASSERT_FALSE (pending.is_initialized ());
		boost::optional<std::string> representative (accounts.second.get_optional<std::string> ("representative"));
		ASSERT_FALSE (representative.is_initialized ());
	}
	// Test for optional values
	request.put ("weight", "1");
	request.put ("pending", "1");
	request.put ("representative", "true");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	for (auto & accounts : response2.json.get_child ("accounts"))
	{
		boost::optional<std::string> weight (accounts.second.get_optional<std::string> ("weight"));
		ASSERT_TRUE (weight.is_initialized ());
		ASSERT_EQ ("0", weight.get ());
		boost::optional<std::string> pending (accounts.second.get_optional<std::string> ("pending"));
		ASSERT_TRUE (pending.is_initialized ());
		ASSERT_EQ ("0", pending.get ());
		boost::optional<std::string> representative (accounts.second.get_optional<std::string> ("representative"));
		ASSERT_TRUE (representative.is_initialized ());
		ASSERT_EQ (rai::test_genesis_key.pub.to_account (), representative.get ());
	}
}

TEST (rpc, accounts_create)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "accounts_create");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("count", "8");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto & accounts (response.json.get_child ("accounts"));
	for (auto i (accounts.begin ()), n (accounts.end ()); i != n; ++i)
	{
		std::string account_text (i->second.get<std::string> (""));
		rai::uint256_union account;
		ASSERT_FALSE (account.decode_account (account_text));
		ASSERT_TRUE (system.wallet (0)->exists (account));
	}
	ASSERT_EQ (8, accounts.size ());
}

TEST (rpc, block_create)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto send_work = node1.work_generate_blocking (latest);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, send_work);
	auto open_work = node1.work_generate_blocking (key.pub);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, open_work);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block_create");
	request.put ("type", "send");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	request.put ("previous", latest.to_string ());
	request.put ("amount", "340282366920938463463374607431768211355");
	request.put ("destination", key.pub.to_account ());
	request.put ("work", rai::to_string_hex (send_work));
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string send_hash (response.json.get<std::string> ("hash"));
	ASSERT_EQ (send.hash ().to_string (), send_hash);
	auto send_text (response.json.get<std::string> ("block"));
	boost::property_tree::ptree block_l;
	std::stringstream block_stream (send_text);
	boost::property_tree::read_json (block_stream, block_l);
	auto send_block (rai::deserialize_block_json (block_l));
	ASSERT_EQ (send.hash (), send_block->hash ());
	system.nodes[0]->process (send);
	boost::property_tree::ptree request1;
	request1.put ("action", "block_create");
	request1.put ("type", "open");
	std::string key_text;
	key.prv.data.encode_hex (key_text);
	request1.put ("key", key_text);
	request1.put ("representative", rai::test_genesis_key.pub.to_account ());
	request1.put ("source", send.hash ().to_string ());
	request1.put ("work", rai::to_string_hex (open_work));
	test_response response1 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response1.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response1.status);
	std::string open_hash (response1.json.get<std::string> ("hash"));
	ASSERT_EQ (open.hash ().to_string (), open_hash);
	auto open_text (response1.json.get<std::string> ("block"));
	std::stringstream block_stream1 (open_text);
	boost::property_tree::read_json (block_stream1, block_l);
	auto open_block (rai::deserialize_block_json (block_l));
	ASSERT_EQ (open.hash (), open_block->hash ());
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	request1.put ("representative", key.pub.to_account ());
	test_response response2 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response2.status);
	std::string open2_hash (response2.json.get<std::string> ("hash"));
	ASSERT_NE (open.hash ().to_string (), open2_hash); // different blocks with wrong representative
	auto change_work = node1.work_generate_blocking (open.hash ());
	rai::change_block change (open.hash (), key.pub, key.prv, key.pub, change_work);
	request1.put ("type", "change");
	request1.put ("work", rai::to_string_hex (change_work));
	test_response response4 (request1, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response4.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response4.status);
	std::string change_hash (response4.json.get<std::string> ("hash"));
	ASSERT_EQ (change.hash ().to_string (), change_hash);
	auto change_text (response4.json.get<std::string> ("block"));
	std::stringstream block_stream4 (change_text);
	boost::property_tree::read_json (block_stream4, block_l);
	auto change_block (rai::deserialize_block_json (block_l));
	ASSERT_EQ (change.hash (), change_block->hash ());
	ASSERT_EQ (rai::process_result::progress, node1.process (change).code);
	rai::send_block send2 (send.hash (), key.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (send.hash ()));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (send2).code);
	boost::property_tree::ptree request2;
	request2.put ("action", "block_create");
	request2.put ("type", "receive");
	request2.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request2.put ("account", key.pub.to_account ());
	request2.put ("source", send2.hash ().to_string ());
	request2.put ("previous", change.hash ().to_string ());
	request2.put ("work", rai::to_string_hex (node1.work_generate_blocking (change.hash ())));
	test_response response5 (request2, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response5.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response5.status);
	std::string receive_hash (response4.json.get<std::string> ("hash"));
	auto receive_text (response5.json.get<std::string> ("block"));
	std::stringstream block_stream5 (change_text);
	boost::property_tree::read_json (block_stream5, block_l);
	auto receive_block (rai::deserialize_block_json (block_l));
	ASSERT_EQ (receive_hash, receive_block->hash ().to_string ());
	system.nodes[0]->process_active (std::move (receive_block));
	latest = system.nodes[0]->latest (key.pub);
	ASSERT_EQ (receive_hash, latest.to_string ());
}

TEST (rpc, block_create_state)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	boost::property_tree::ptree request;
	request.put ("action", "block_create");
	request.put ("type", "state");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("account", rai::test_genesis_key.pub.to_account ());
	request.put ("previous", genesis.hash ().to_string ());
	request.put ("representative", rai::test_genesis_key.pub.to_account ());
	request.put ("balance", (rai::genesis_amount - rai::Gxrb_ratio).convert_to<std::string> ());
	request.put ("link", key.pub.to_account ());
	request.put ("work", rai::to_string_hex (system.nodes[0]->work_generate_blocking (genesis.hash ())));
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string state_hash (response.json.get<std::string> ("hash"));
	auto state_text (response.json.get<std::string> ("block"));
	std::stringstream block_stream (state_text);
	boost::property_tree::ptree block_l;
	boost::property_tree::read_json (block_stream, block_l);
	auto state_block (rai::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, state_block);
	ASSERT_EQ (rai::block_type::state, state_block->type ());
	ASSERT_EQ (state_hash, state_block->hash ().to_string ());
	auto process_result (system.nodes[0]->process (*state_block));
	ASSERT_EQ (rai::process_result::progress, process_result.code);
}

TEST (rpc, block_create_state_open)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto send_block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Gxrb_ratio));
	ASSERT_NE (nullptr, send_block);
	boost::property_tree::ptree request;
	request.put ("action", "block_create");
	request.put ("type", "state");
	request.put ("key", key.prv.data.to_string ());
	request.put ("account", key.pub.to_account ());
	request.put ("previous", 0);
	request.put ("representative", rai::test_genesis_key.pub.to_account ());
	request.put ("balance", rai::Gxrb_ratio.convert_to<std::string> ());
	request.put ("link", send_block->hash ().to_string ());
	request.put ("work", rai::to_string_hex (system.nodes[0]->work_generate_blocking (send_block->hash ())));
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string state_hash (response.json.get<std::string> ("hash"));
	auto state_text (response.json.get<std::string> ("block"));
	std::stringstream block_stream (state_text);
	boost::property_tree::ptree block_l;
	boost::property_tree::read_json (block_stream, block_l);
	auto state_block (rai::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, state_block);
	ASSERT_EQ (rai::block_type::state, state_block->type ());
	ASSERT_EQ (state_hash, state_block->hash ().to_string ());
	ASSERT_TRUE (system.nodes[0]->latest (key.pub).is_zero ());
	auto process_result (system.nodes[0]->process (*state_block));
	ASSERT_EQ (rai::process_result::progress, process_result.code);
	ASSERT_FALSE (system.nodes[0]->latest (key.pub).is_zero ());
}

// Missing "work" parameter should cause work to be generated for us.
TEST (rpc, block_create_state_request_work)
{
	rai::genesis genesis;

	// Test work generation for state blocks both with and without previous (in the latter
	// case, the account will be used for work generation)
	std::vector<std::string> previous_test_input{ genesis.hash ().to_string (), std::string ("0") };
	for (auto previous : previous_test_input)
	{
		rai::system system (24000, 1);
		rai::keypair key;
		rai::genesis genesis;
		system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
		boost::property_tree::ptree request;
		request.put ("action", "block_create");
		request.put ("type", "state");
		request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
		request.put ("account", rai::test_genesis_key.pub.to_account ());
		request.put ("representative", rai::test_genesis_key.pub.to_account ());
		request.put ("balance", (rai::genesis_amount - rai::Gxrb_ratio).convert_to<std::string> ());
		request.put ("link", key.pub.to_account ());
		request.put ("previous", previous);
		rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
		rpc.start ();
		test_response response (request, rpc, system.io_ctx);
		system.deadline_set (5s);
		while (response.status == 0)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_EQ (200, response.status);
		boost::property_tree::ptree block_l;
		std::stringstream block_stream (response.json.get<std::string> ("block"));
		boost::property_tree::read_json (block_stream, block_l);
		auto block (rai::deserialize_block_json (block_l));
		ASSERT_NE (nullptr, block);
		ASSERT_FALSE (rai::work_validate (*block));
	}
}

TEST (rpc, block_hash)
{
	rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes[0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	rai::rpc rpc (system.io_ctx, node1, rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block_hash");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string send_hash (response.json.get<std::string> ("hash"));
	ASSERT_EQ (send.hash ().to_string (), send_hash);
}

TEST (rpc, wallet_lock)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	{
		auto transaction (system.wallet (0)->wallets.tx_begin ());
		ASSERT_TRUE (system.wallet (0)->store.valid_password (transaction));
	}
	request.put ("wallet", wallet);
	request.put ("action", "wallet_lock");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("locked"));
	ASSERT_EQ (account_text1, "1");
	auto transaction (system.wallet (0)->wallets.tx_begin ());
	ASSERT_FALSE (system.wallet (0)->store.valid_password (transaction));
}

TEST (rpc, wallet_locked)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_locked");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string account_text1 (response.json.get<std::string> ("locked"));
	ASSERT_EQ (account_text1, "0");
}

TEST (rpc, wallet_create_fail)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	auto node = system.nodes[0];
	// lmdb_max_dbs should be removed once the wallet store is refactored to support more wallets.
	for (int i = 0; i < 113; i++)
	{
		rai::keypair key;
		node->wallets.create (key.pub);
	}
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_create");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ ("Failed to create wallet. Increase lmdb_max_dbs in node config", response.json.get<std::string> ("error"));
}

TEST (rpc, wallet_ledger)
{
	rai::system system (24000, 1);
	rai::keypair key;
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (key.prv);
	auto & node1 (*system.nodes[0]);
	auto latest (system.nodes[0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.work_generate_blocking (latest));
	system.nodes[0]->process (send);
	rai::open_block open (send.hash (), rai::test_genesis_key.pub, key.pub, key.prv, key.pub, node1.work_generate_blocking (key.pub));
	ASSERT_EQ (rai::process_result::progress, system.nodes[0]->process (open).code);
	auto time (rai::seconds_since_epoch ());
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "wallet_ledger");
	request.put ("wallet", system.nodes[0]->wallets.items.begin ()->first.to_string ());
	request.put ("sorting", "1");
	request.put ("count", "1");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	for (auto & accounts : response.json.get_child ("accounts"))
	{
		std::string account_text (accounts.first);
		ASSERT_EQ (key.pub.to_account (), account_text);
		std::string frontier (accounts.second.get<std::string> ("frontier"));
		ASSERT_EQ (open.hash ().to_string (), frontier);
		std::string open_block (accounts.second.get<std::string> ("open_block"));
		ASSERT_EQ (open.hash ().to_string (), open_block);
		std::string representative_block (accounts.second.get<std::string> ("representative_block"));
		ASSERT_EQ (open.hash ().to_string (), representative_block);
		std::string balance_text (accounts.second.get<std::string> ("balance"));
		ASSERT_EQ ("340282366920938463463374607431768211355", balance_text);
		std::string modified_timestamp (accounts.second.get<std::string> ("modified_timestamp"));
		ASSERT_LT (std::abs ((long)time - stol (modified_timestamp)), 5);
		std::string block_count (accounts.second.get<std::string> ("block_count"));
		ASSERT_EQ ("1", block_count);
		boost::optional<std::string> weight (accounts.second.get_optional<std::string> ("weight"));
		ASSERT_FALSE (weight.is_initialized ());
		boost::optional<std::string> pending (accounts.second.get_optional<std::string> ("pending"));
		ASSERT_FALSE (pending.is_initialized ());
		boost::optional<std::string> representative (accounts.second.get_optional<std::string> ("representative"));
		ASSERT_FALSE (representative.is_initialized ());
	}
	// Test for optional values
	request.put ("weight", "true");
	request.put ("pending", "1");
	request.put ("representative", "false");
	test_response response2 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	for (auto & accounts : response2.json.get_child ("accounts"))
	{
		boost::optional<std::string> weight (accounts.second.get_optional<std::string> ("weight"));
		ASSERT_TRUE (weight.is_initialized ());
		ASSERT_EQ ("0", weight.get ());
		boost::optional<std::string> pending (accounts.second.get_optional<std::string> ("pending"));
		ASSERT_TRUE (pending.is_initialized ());
		ASSERT_EQ ("0", pending.get ());
		boost::optional<std::string> representative (accounts.second.get_optional<std::string> ("representative"));
		ASSERT_FALSE (representative.is_initialized ());
	}
}

TEST (rpc, wallet_add_watch)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	std::string wallet;
	system.nodes[0]->wallets.items.begin ()->first.encode_hex (wallet);
	request.put ("wallet", wallet);
	request.put ("action", "wallet_add_watch");
	boost::property_tree::ptree entry;
	boost::property_tree::ptree peers_l;
	entry.put ("", rai::test_genesis_key.pub.to_account ());
	peers_l.push_back (std::make_pair ("", entry));
	request.add_child ("accounts", peers_l);
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	std::string success (response.json.get<std::string> ("success"));
	ASSERT_TRUE (success.empty ());
	ASSERT_TRUE (system.wallet (0)->exists (rai::test_genesis_key.pub));
}

TEST (rpc, online_reps)
{
	rai::system system (24000, 2);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	ASSERT_TRUE (system.nodes[1]->online_reps.online_stake () == system.nodes[1]->config.online_weight_minimum.number ());
	system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, rai::Gxrb_ratio);
	system.deadline_set (10s);
	while (system.nodes[1]->online_reps.online_stake () == system.nodes[1]->config.online_weight_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[1], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "representatives_online");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto representatives (response.json.get_child ("representatives"));
	auto item (representatives.begin ());
	ASSERT_NE (representatives.end (), item);
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), item->first);
	boost::optional<std::string> weight (item->second.get_optional<std::string> ("weight"));
	ASSERT_FALSE (weight.is_initialized ());
	//Test weight option
	request.put ("weight", "true");
	test_response response2 (request, rpc, system.io_ctx);
	while (response2.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto representatives2 (response2.json.get_child ("representatives"));
	auto item2 (representatives2.begin ());
	ASSERT_NE (representatives2.end (), item2);
	ASSERT_EQ (rai::test_genesis_key.pub.to_account (), item2->first);
	auto weight2 (item2->second.get<std::string> ("weight"));
	ASSERT_EQ (system.nodes[1]->weight (rai::test_genesis_key.pub).convert_to<std::string> (), weight2);
	//Test accounts filter
	system.wallet (1)->insert_adhoc (rai::test_genesis_key.prv);
	auto new_rep (system.wallet (1)->deterministic_insert ());
	auto send (system.wallet (1)->send_action (rai::test_genesis_key.pub, new_rep, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send);
	auto receive (system.wallet (1)->receive_action (static_cast<rai::send_block &> (*send), new_rep, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, receive);
	auto change (system.wallet (1)->change_action (rai::test_genesis_key.pub, new_rep));
	ASSERT_NE (nullptr, change);
	system.deadline_set (5s);
	while (system.nodes[1]->online_reps.list ().size () != 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	boost::property_tree::ptree child_rep;
	child_rep.put ("", new_rep.to_account ());
	boost::property_tree::ptree filtered_accounts;
	filtered_accounts.push_back (std::make_pair ("", child_rep));
	request.add_child ("accounts", filtered_accounts);
	test_response response3 (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response3.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto representatives3 (response3.json.get_child ("representatives"));
	auto item3 (representatives3.begin ());
	ASSERT_NE (representatives3.end (), item3);
	ASSERT_EQ (new_rep.to_account (), item3->first);
	ASSERT_EQ (representatives3.size (), 1);
	system.nodes[1]->stop ();
}

TEST (rpc, confirmation_history)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, rai::test_genesis_key.pub, rai::Gxrb_ratio));
	ASSERT_TRUE (system.nodes[0]->active.confirmed.empty ());
	system.deadline_set (10s);
	while (system.nodes[0]->active.confirmed.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "confirmation_history");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto representatives (response.json.get_child ("confirmations"));
	auto item (representatives.begin ());
	ASSERT_NE (representatives.end (), item);
	auto hash (item->second.get<std::string> ("hash"));
	auto tally (item->second.get<std::string> ("tally"));
	ASSERT_EQ (block->hash ().to_string (), hash);
	rai::amount tally_num;
	tally_num.decode_dec (tally);
	assert (tally_num == rai::genesis_amount || tally_num == (rai::genesis_amount - rai::Gxrb_ratio));
	system.stop ();
}

TEST (rpc, block_confirm)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::genesis genesis;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto send1 (std::make_shared<rai::state_block> (rai::test_genesis_key.pub, genesis.hash (), rai::test_genesis_key.pub, rai::genesis_amount - rai::Gxrb_ratio, rai::test_genesis_key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	{
		auto transaction (system.nodes[0]->store.tx_begin (true));
		ASSERT_EQ (rai::process_result::progress, system.nodes[0]->ledger.process (transaction, *send1).code);
	}
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block_confirm");
	request.put ("hash", send1->hash ().to_string ());
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ ("1", response.json.get<std::string> ("started"));
}

TEST (rpc, block_confirm_absent)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "block_confirm");
	request.put ("hash", "0");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ ("Block not found", response.json.get<std::string> ("error"));
}

TEST (rpc, node_id)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	boost::property_tree::ptree request;
	request.put ("action", "node_id");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	rai::keypair node_id (system.nodes[0]->store.get_node_id (transaction));
	ASSERT_EQ (node_id.prv.data.to_string (), response.json.get<std::string> ("private"));
	ASSERT_EQ (node_id.pub.to_account (), response.json.get<std::string> ("as_account"));
}

TEST (rpc, node_id_delete)
{
	rai::system system (24000, 1);
	rai::rpc rpc (system.io_ctx, *system.nodes[0], rai::rpc_config (true));
	rpc.start ();
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		rai::keypair node_id (system.nodes[0]->store.get_node_id (transaction));
		ASSERT_EQ (node_id.pub.to_string (), system.nodes[0]->node_id.pub.to_string ());
	}
	boost::property_tree::ptree request;
	request.put ("action", "node_id_delete");
	test_response response (request, rpc, system.io_ctx);
	system.deadline_set (5s);
	while (response.status == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (200, response.status);
	ASSERT_EQ ("1", response.json.get<std::string> ("deleted"));
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	rai::keypair node_id (system.nodes[0]->store.get_node_id (transaction));
	ASSERT_NE (node_id.pub.to_string (), system.nodes[0]->node_id.pub.to_string ());
}
