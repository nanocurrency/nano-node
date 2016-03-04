#include <gtest/gtest.h>

#include <rai/node/testing.hpp>
#include <rai/node/rpc.hpp>

#include <boost/network/protocol/http/client.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/thread.hpp>

std::pair <boost::property_tree::ptree, boost::network::http::server <rai::rpc>::response::status_type> test_response (boost::property_tree::ptree const & request_a, rai::rpc & rpc_a)
{
	std::pair <boost::property_tree::ptree, boost::network::http::server <rai::rpc>::response::status_type> result;
	boost::network::http::client client;
	auto url ("http://[::1]:" + std::to_string (rpc_a.config.port));
	boost::network::http::client::request request (url);
	std::string request_string;
	{
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, request_a);
		request_string = ostream.str ();
	}
	request.add_header (std::make_pair ("content-length", std::to_string (request_string.size ())));
	try
	{
		boost::network::http::client::response response = client.post (request, request_string);
		uint16_t status (boost::network::http::status (response));
		result.second = static_cast <boost::network::http::server <rai::rpc>::response::status_type> (status);
		std::string body_l (boost::network::http::body (response));
		if (result.second == boost::network::http::server <rai::rpc>::response::ok)
		{
			std::stringstream istream (response.body ());
			boost::property_tree::read_json (istream, result.first);
		}
	}
	catch (std::runtime_error const & error)
	{
	}
	return result;
}

TEST (rpc, account_balance)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "account_balance");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
    auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string balance_text (response.first.get <std::string> ("balance"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
	rpc.stop ();
	thread1.join ();
}

TEST (rpc, account_create)
{
	rai::system system (24000, 1);
	auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
	rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	boost::property_tree::ptree request;
	request.put ("action", "account_create");
	request.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
	auto response (test_response (request, rpc));
	ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	auto account_text (response.first.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (system.wallet (0)->exists (account));
	rpc.stop();
	thread1.join();
}

TEST (rpc, account_weight)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::block_hash latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
    rai::change_block block (latest, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
	ASSERT_EQ (rai::process_result::progress, node1.process (block).code);
	auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "account_weight");
    request.put ("account", key.pub.to_account ());
	auto response (test_response (request, rpc));
	ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string balance_text (response.first.get <std::string> ("weight"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_contains)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_contains");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string exists_text (response.first.get <std::string> ("exists"));
    ASSERT_EQ ("1", exists_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_doesnt_contain)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_contains");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string exists_text (response.first.get <std::string> ("exists"));
    ASSERT_EQ ("0", exists_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, validate_account_number)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "validate_account_number");
    request.put ("account", rai::test_genesis_key.pub.to_account ());
	auto response (test_response (request, rpc));
    std::string exists_text (response.first.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, validate_account_invalid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    std::string account;
    rai::test_genesis_key.pub.encode_account (account);
    account [0] ^= 0x1;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "validate_account_number");
    request.put ("account", account);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string exists_text (response.first.get <std::string> ("valid"));
    ASSERT_EQ ("0", exists_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, send)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    system.wallet (0)->insert (rai::test_genesis_key.prv);
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
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string block_text (response.first.get <std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (system.nodes [0]->ledger.block_exists (block));
	rpc.stop ();
	thread1.join ();
	thread2.join ();
}

TEST (rpc, send_fail)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
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
	auto response (test_response (request, rpc));
	done = true;
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string block_text (response.first.get <std::string> ("block"));
	rai::block_hash block;
	ASSERT_FALSE (block.decode_hex (block_text));
	ASSERT_TRUE (block.is_zero ());
	rpc.stop ();
	thread1.join ();
	thread2.join ();
}

TEST (rpc, wallet_add)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    rai::keypair key1;
    std::string key_text;
    key1.prv.data.encode_hex (key_text);
	system.wallet (0)->insert (key1.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_add");
    request.put ("key", key_text);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string account_text1 (response.first.get <std::string> ("account"));
    ASSERT_EQ (account_text1, key1.pub.to_account ());
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_password_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_valid");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string account_text1 (response.first.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_password_change)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_change");
    request.put ("password", "test");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string account_text1 (response.first.get <std::string> ("changed"));
    ASSERT_EQ (account_text1, "1");
    ASSERT_TRUE (system.wallet (0)->valid_password ());
    ASSERT_TRUE (system.wallet (0)->enter_password (""));
    ASSERT_FALSE (system.wallet (0)->valid_password ());
    ASSERT_FALSE (system.wallet (0)->enter_password ("test"));
    ASSERT_TRUE (system.wallet (0)->valid_password ());
	rpc.stop();
	thread1.join();
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
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "password_enter");
    request.put ("password", "");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string account_text1 (response.first.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
	rpc.stop();
	thread1.join();
}

TEST (rpc, representative)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "representative");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string account_text1 (response.first.get <std::string> ("representative"));
    ASSERT_EQ (account_text1, rai::genesis_account.to_account ());
	rpc.stop();
	thread1.join();
}

TEST (rpc, representative_set)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    rai::keypair key;
    request.put ("action", "representative_set");
    request.put ("representative", key.pub.to_account ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (key.pub, system.nodes [0]->wallets.items.begin ()->second->store.representative (transaction));
	rpc.stop();
	thread1.join();
}

TEST (rpc, account_list)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    rai::keypair key2;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key2.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "account_list");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & accounts_node (response.first.get_child ("accounts"));
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
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_key_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request.put ("wallet", wallet);
    request.put ("action", "wallet_key_valid");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string exists_text (response.first.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "wallet_create");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string wallet_text (response.first.get <std::string> ("wallet"));
    rai::uint256_union wallet_id;
    ASSERT_FALSE (wallet_id.decode_hex (wallet_text));
    ASSERT_NE (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_export)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "wallet_export");
    request.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    std::string wallet_json (response.first.get <std::string> ("json"));
    bool error (false);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
	rai::kdf kdf;
    rai::wallet_store store (error, kdf, transaction, rai::genesis_account, 1, "0", wallet_json);
    ASSERT_FALSE (error);
    ASSERT_TRUE (store.exists (transaction, rai::test_genesis_key.pub));
	rpc.stop();
	thread1.join();
}

TEST (rpc, wallet_destroy)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "wallet_destroy");
    request.put ("wallet", wallet_id.to_string ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    ASSERT_EQ (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
	rpc.stop();
	thread1.join();
}

TEST (rpc, account_move)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    auto destination (system.wallet (0));
    rai::keypair key;
	destination->insert (rai::test_genesis_key.prv);
    rai::keypair source_id;
    auto source (system.nodes [0]->wallets.create (source_id.pub));
	source->insert (key.prv);
    boost::property_tree::ptree request;
    request.put ("action", "account_move");
    request.put ("wallet", wallet_id.to_string ());
    request.put ("source", source_id.pub.to_string ());
    boost::property_tree::ptree keys;
    boost::property_tree::ptree entry;
    entry.put ("", key.pub.to_string ());
    keys.push_back (std::make_pair ("", entry));
    request.add_child ("accounts", keys);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    ASSERT_EQ ("1", response.first.get <std::string> ("moved"));
    ASSERT_TRUE (destination->exists (key.pub));
    ASSERT_TRUE (destination->exists (rai::test_genesis_key.pub));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (source->store.end (), source->store.begin (transaction));
	rpc.stop();
	thread1.join();
}

TEST (rpc, block)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "block");
	request.put ("hash", system.nodes [0]->latest (rai::genesis_account).to_string ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	auto contents (response.first.get <std::string> ("contents"));
    ASSERT_FALSE (contents.empty ());
	rpc.stop();
	thread1.join();
}

TEST (rpc, chain)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "chain");
	request.put ("block", block->hash().to_string ());
	request.put ("count", std::to_string (std::numeric_limits <uint64_t>::max ()));
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & blocks_node (response.first.get_child ("blocks"));
	std::vector <rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (block->hash(), blocks [0]);
	ASSERT_EQ (genesis, blocks [1]);
	rpc.stop();
	thread1.join();
}

TEST (rpc, chain_limit)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::keypair key;
	auto genesis (system.nodes [0]->latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (genesis.is_zero ());
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	ASSERT_NE (nullptr, block);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "chain");
	request.put ("block", block->hash().to_string ());
	request.put ("count", 1);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & blocks_node (response.first.get_child ("blocks"));
	std::vector <rai::block_hash> blocks;
	for (auto i (blocks_node.begin ()), n (blocks_node.end ()); i != n; ++i)
	{
		blocks.push_back (rai::block_hash (i->second.get <std::string> ("")));
	}
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block->hash(), blocks [0]);
	rpc.stop();
	thread1.join();
}

TEST (rpc, process_block)
{
    rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	auto & node1 (*system.nodes [0]);
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, node1.generate_work (latest));
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request.put ("block", json);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	ASSERT_EQ (send.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
	rpc.stop();
	thread1.join();
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
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (std::numeric_limits <uint64_t>::max ()));
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & frontiers_node (response.first.get_child ("frontiers"));
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
	rpc.stop();
	thread1.join();
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
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", rai::account (0).to_account ());
	request.put ("count", std::to_string (100));
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & frontiers_node (response.first.get_child ("frontiers"));
	ASSERT_EQ (100, frontiers_node.size ());
	rpc.stop();
	thread1.join();
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
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv.data, 0, 0, 0, 0));
		}
	}
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "frontiers");
	request.put ("account", source.begin ()->first.to_account ());
	request.put ("count", std::to_string (1));
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & frontiers_node (response.first.get_child ("frontiers"));
	ASSERT_EQ (1, frontiers_node.size ());
	ASSERT_EQ (source.begin ()->first.to_account (), frontiers_node.begin ()->first);
	rpc.stop();
	thread1.join();
}

TEST (rpc, peers)
{
    rai::system system (24000, 2);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "peers");
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
    auto & frontiers_node (response.first.get_child ("peers"));
	ASSERT_EQ (1, frontiers_node.size ());
	rpc.stop();
	thread1.join();
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
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	auto wallet (system.nodes [0]->wallets.items.begin ()->first.to_string ());
	rai::send_block block (system.nodes [0]->latest (rai::test_genesis_key.pub), rai::test_genesis_key.pub, rai::genesis_amount - system.nodes [0]->config.receive_minimum.number (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (rai::transaction (system.nodes [0]->store.environment, nullptr, true), block).code);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "search_pending");
	request.put ("wallet", wallet);
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	auto iterations (0);
	while (system.nodes [0]->balance (rai::test_genesis_key.pub) != rai::genesis_amount)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	rpc.stop();
	thread1.join();
}

TEST (rpc, keepalive)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, *system.service, 24001, rai::unique_path (), system.alarm, system.logging, system.work));
    node1->start ();
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
    request.put ("action", "keepalive");
    auto address (boost::str (boost::format ("%1%") % node1->network.endpoint ().address ()));
    auto port (boost::str (boost::format ("%1%") % node1->network.endpoint ().port ()));
	request.put ("address", address);
	request.put ("port", port);
	ASSERT_FALSE (system.nodes [0]->peers.known_peer (node1->network.endpoint ()));
	ASSERT_EQ (0, system.nodes [0]->peers.size());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	auto iterations (0);
	while (!system.nodes [0]->peers.known_peer (node1->network.endpoint ()))
	{
		ASSERT_EQ (0, system.nodes [0]->peers.size());
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
	rpc.stop();
	thread1.join();
}

TEST (rpc, payment_init)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request;
	request.put ("action", "payment_init");
	request.put ("wallet", wallet_id.pub.to_string ());
	auto response (test_response (request, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.second);
	ASSERT_EQ ("Ready", response.first.get <std::string> ("status"));
	rpc.stop();
	thread1.join();
}

TEST (rpc, payment_begin_end)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	auto account_text (response1.first.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
    boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	auto response2 (test_response (request2, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response2.second);
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_NE (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	rpc.stop();
	thread1.join();
}

TEST (rpc, payment_end_nonempty)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->init_free_accounts (rai::transaction (node1->store.environment, nullptr, false));
	auto wallet_id (node1->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_end");
	request1.put ("wallet", wallet_id.to_string ());
	request1.put ("account", rai::test_genesis_key.pub.to_account ());
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::bad_request, response1.second);
	rpc.stop();
	thread1.join();
}

TEST (rpc, payment_zero_balance)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->init_free_accounts (rai::transaction (node1->store.environment, nullptr, false));
	auto wallet_id (node1->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.to_string ());
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	auto account_text (response1.first.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_NE (rai::test_genesis_key.pub, account);
	rpc.stop();
	thread1.join();
}

TEST (rpc, payment_begin_reuse)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair wallet_id;
	auto wallet (node1->wallets.create (wallet_id.pub));
	ASSERT_TRUE (node1->wallets.items.find (wallet_id.pub) != node1->wallets.items.end ());
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_begin");
	request1.put ("wallet", wallet_id.pub.to_string ());
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	auto account_text (response1.first.get <std::string> ("account"));
	rai::uint256_union account;
	ASSERT_FALSE (account.decode_account (account_text));
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_EQ (wallet->free_accounts.end (), wallet->free_accounts.find (account));
    boost::property_tree::ptree request2;
	request2.put ("action", "payment_end");
	request2.put ("wallet", wallet_id.pub.to_string ());
	request2.put ("account", account.to_account ());
	auto response2 (test_response (request2, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response2.second);
	ASSERT_TRUE (wallet->exists (account));
	ASSERT_NE (wallet->free_accounts.end (), wallet->free_accounts.find (account));
	auto response3 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response3.second);
	auto account2_text (response1.first.get <std::string> ("account"));
	rai::uint256_union account2;
	ASSERT_FALSE (account2.decode_account (account2_text));
	ASSERT_EQ (account, account2);
	rpc.stop();
	thread1.join();
}

TEST (rpc, DISABLED_payment_wait)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
	rai::thread_runner runner (*system.service, node1->config.io_threads);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "payment_wait");
	request1.put ("account", key.pub.to_account ());
	request1.put ("amount", rai::amount (rai::Mrai_ratio).to_string_dec ());
	request1.put ("timeout", "100");
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("nothing", response1.first.get <std::string> ("status"));
	request1.put ("timeout", "100000");
	system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mrai_ratio);
	system.alarm.add (std::chrono::system_clock::now () + std::chrono::milliseconds(500), [&] ()
	{
		system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, rai::Mrai_ratio);
	});
	auto response2 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response2.second);
	ASSERT_EQ ("success", response2.first.get <std::string> ("status"));
	request1.put ("amount", rai::amount (rai::Mrai_ratio * 2).to_string_dec ());
	auto response3 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response3.second);
	ASSERT_EQ ("success", response2.first.get <std::string> ("status"));
	node1->stop ();
	rpc.stop();
	runner.join ();
	thread1.join();
}

TEST (rpc, version)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "version");
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("1", response1.first.get <std::string> ("rpc_version"));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("2", response1.first.get <std::string> ("store_version"));
	rpc.stop();
	thread1.join ();
}

TEST (rpc, work_generate)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	rai::block_hash hash1 (1);
    boost::property_tree::ptree request1;
	request1.put ("action", "work_generate");
	request1.put ("hash", hash1.to_string ());
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	auto work1 (response1.first.get <std::string> ("work"));
	uint64_t work2;
	ASSERT_FALSE (rai::from_string_hex (work1, work2));
	ASSERT_FALSE (system.work.work_validate (hash1, work2));
	rpc.stop();
	thread1.join ();
}

TEST (rpc, work_cancel)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	rai::block_hash hash1 (1);
    boost::property_tree::ptree request1;
	request1.put ("action", "work_cancel");
	request1.put ("hash", hash1.to_string ());
	boost::optional <uint64_t> work;
	std::thread thread ([&] ()
	{
		work = system.work.generate_maybe (hash1);
	});
	auto response1 (test_response (request1, rpc));
	ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	thread.join ();
	rpc.stop();
	thread1.join ();
}

TEST (rpc, work_peer_bad)
{
    rai::system system (24000, 2);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	rai::thread_runner runner (*system.service, node1.config.io_threads);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	node2.config.work_peers.push_back (std::make_pair (boost::asio::ip::address_v6::any (), 0));
	rai::block_hash hash1 (1);
	auto work (node2.generate_work (hash1));
	ASSERT_FALSE (system.work.work_validate (hash1, work));
	rpc.stop ();
	node1.stop ();
	node2.stop ();
	runner.join ();
	thread1.join ();
}

TEST (rpc, work_peer_one)
{
    rai::system system (24000, 2);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	rai::thread_runner runner (*system.service, node1.config.io_threads);
	rai::keypair key;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key.prv);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
	node2.config.work_peers.push_back (std::make_pair (node1.network.endpoint ().address (), rpc.config.port));
	rai::keypair key1;
	auto work (node2.generate_work (key1.pub));
	ASSERT_FALSE (system.work.work_validate (key1.pub, work));
	rpc.stop ();
	runner.join ();
	thread1.join ();
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
	rai::thread_runner runner1 (*system1.service, node1.config.io_threads);
	rai::thread_runner runner2 (*system2.service, node2.config.io_threads);
	rai::thread_runner runner3 (*system3.service, node3.config.io_threads);
	rai::thread_runner runner4 (*system4.service, node4.config.io_threads);
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> (16));
	rai::rpc_config config2 (true);
	config2.port += 0;
    rai::rpc rpc2 (system2.service, pool, node2, config2);
	rpc2.start ();
	std::thread thread2 ([&] () {rpc2.server.run();});
	rai::rpc_config config3 (true);
	config3.port += 1;
    rai::rpc rpc3 (system3.service, pool, node3, config3);
	rpc3.start ();
	std::thread thread3 ([&] () {rpc3.server.run();});
	rai::rpc_config config4 (true);
	config4.port += 2;
    rai::rpc rpc4 (system4.service, pool, node4, config4);
	rpc4.start ();
	std::thread thread4 ([&] () {rpc4.server.run();});
	node1.config.work_peers.push_back (std::make_pair (node2.network.endpoint ().address (), rpc2.config.port));
	node1.config.work_peers.push_back (std::make_pair (node3.network.endpoint ().address (), rpc3.config.port));
	node1.config.work_peers.push_back (std::make_pair (node4.network.endpoint ().address (), rpc4.config.port));
	for (auto i (0); i < 10; ++i)
	{
		rai::keypair key1;
		auto work (node1.generate_work (key1.pub));
		ASSERT_FALSE (system1.work.work_validate (key1.pub, work));
	}
	rpc2.stop ();
	rpc3.stop ();
	rpc4.stop ();
	thread2.join ();
	thread3.join ();
	thread4.join ();
	system1.stop ();
	system2.stop ();
	system3.stop ();
	system4.stop ();
	runner1.join ();
	runner2.join ();
	runner3.join ();
	runner4.join ();
}

TEST (rpc, block_count)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "block_count");
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("1", response1.first.get <std::string> ("count"));
	rpc.stop ();
	node1.stop ();
	thread1.join ();
}

TEST (rpc, frontier_count)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "frontier_count");
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("1", response1.first.get <std::string> ("count"));
	rpc.stop ();
	node1.stop ();
	thread1.join ();
}

TEST (rpc, available_supply)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto & node1 (*system.nodes [0]);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, node1, rai::rpc_config (true));
	rpc.start ();
	std::thread thread1 ([&rpc] () {rpc.server.run();});
    boost::property_tree::ptree request1;
	request1.put ("action", "available_supply");
	auto response1 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response1.second);
	ASSERT_EQ ("0", response1.first.get <std::string> ("available"));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	rai::keypair key;
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key.pub, 1));
	auto response2 (test_response (request1, rpc));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response2.second);
	ASSERT_EQ ("1", response2.first.get <std::string> ("available"));
	rpc.stop ();
	node1.stop ();
	thread1.join ();
}
