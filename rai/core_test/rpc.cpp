#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <rai/node.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

TEST (rpc, account_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_create");
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto account_text (response_tree.get <std::string> ("account"));
    rai::uint256_union account;
    ASSERT_FALSE (account.decode_base58check (account_text));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_NE (system.wallet (0)->store.end (), system.wallet (0)->store.find (transaction, account));
}

TEST (rpc, account_balance_exact)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance_exact");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, account_balance)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("3402823669209384634", balance_text);
}

TEST (rpc, account_weight_exact)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::frontier frontier;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier));
	}
    rai::change_block block (key.pub, frontier.hash, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (frontier.hash));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (transaction, block));
	}
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_weight_exact");
    request_tree.put ("account", key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("weight"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, account_weight)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::frontier frontier;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier));
	}
    rai::change_block block (key.pub, frontier.hash, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (frontier.hash));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (transaction, block));
	}
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_weight");
    request_tree.put ("account", key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("weight"));
    ASSERT_EQ ("3402823669209384634", balance_text);
}

TEST (rpc, wallet_contains)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("exists"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_doesnt_contain)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("exists"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, validate_account_number)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account_number");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, validate_account_invalid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    account [0] ^= 0x1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account_number");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, send_exact)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    rai::keypair key1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.insert (transaction, key1.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send_exact");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    request_tree.put ("amount", "100");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("1", sent_text);
}

TEST (rpc, send)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    rai::keypair key1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
		system.wallet (0)->store.insert (transaction, key1.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    request_tree.put ("amount", "1");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    auto balance1 (system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
    rpc (request, response);
    ASSERT_EQ (balance1 - rai::scale_64bit_base10, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("1", sent_text);
}

TEST (rpc, send_fail)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    rai::keypair key1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, key1.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
    request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
    request_tree.put ("amount", "100");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("0", sent_text);
}

TEST (rpc, wallet_add)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    rai::keypair key1;
    std::string key_text;
    key1.prv.encode_hex (key_text);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, key1.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_add");
    request_tree.put ("key", key_text);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("account"));
    ASSERT_EQ (account_text1, key1.pub.to_base58check ());
}

TEST (rpc, wallet_password_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_valid");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_password_change)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_change");
    request_tree.put ("password", "test");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("changed"));
    ASSERT_EQ (account_text1, "1");
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_TRUE (system.wallet (0)->store.valid_password (transaction));
    system.wallet (0)->store.enter_password (transaction, "");
    ASSERT_FALSE (system.wallet (0)->store.valid_password (transaction));
    system.wallet (0)->store.enter_password (transaction, "test");
    ASSERT_TRUE (system.wallet (0)->store.valid_password (transaction));
}

TEST (rpc, wallet_password_enter)
{
    rai::system system (24000, 1);
	auto iterations (0);
	while (system.wallet (0)->store.password.value () == 0)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_enter");
    request_tree.put ("password", "");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (rpc, representative)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "representative");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("representative"));
    ASSERT_EQ (account_text1, rai::genesis_account.to_base58check ());
}

TEST (rpc, representative_set)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    rai::keypair key;
    request_tree.put ("action", "representative_set");
    request_tree.put ("representative", key.pub.to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (key.pub, system.nodes [0]->wallets.items.begin ()->second->store.representative (transaction));
}

TEST (rpc, account_list)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
    system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (0)->store.insert (transaction, key2.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "account_list");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto & accounts_node (response_tree.get_child ("accounts"));
    std::vector <rai::uint256_union> accounts;
    for (auto i (accounts_node.begin ()), j (accounts_node.end ()); i != j; ++i)
    {
        auto account (i->second.get <std::string> (""));
        rai::uint256_union number;
        ASSERT_FALSE (number.decode_base58check (account));
        accounts.push_back (number);
    }
    ASSERT_EQ (2, accounts.size ());
    for (auto i (accounts.begin ()), j (accounts.end ()); i != j; ++i)
    {
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
        ASSERT_NE (system.wallet (0)->store.end (), system.wallet (0)->store.find (transaction, *i));
    }
}

TEST (rpc, wallet_key_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
    system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_key_valid");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_create");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string wallet_text (response_tree.get <std::string> ("wallet"));
    rai::uint256_union wallet_id;
    ASSERT_FALSE (wallet_id.decode_hex (wallet_text));
    ASSERT_NE (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
}

TEST (rpc, wallet_export)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
    system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_export");
    request_tree.put ("wallet", system.nodes [0]->wallets.items.begin ()->first.to_string ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string wallet_json (response_tree.get <std::string> ("json"));
    bool error (false);
    rai::wallet_store store (error, transaction, "0", wallet_json);
    ASSERT_FALSE (error);
    ASSERT_TRUE (store.exists (transaction, rai::test_genesis_key.pub));
}

TEST (rpc, wallet_destroy)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		system.wallet (0)->store.insert (transaction, rai::test_genesis_key.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_destroy");
    request_tree.put ("wallet", wallet_id.to_string ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    ASSERT_EQ (system.nodes [0]->wallets.items.end (), system.nodes [0]->wallets.items.find (wallet_id));
}

TEST (rpc, account_move)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    auto destination (system.wallet (0));
    rai::keypair key;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		destination->store.insert (transaction, rai::test_genesis_key.prv);
	}
    rai::keypair source_id;
    auto source (system.nodes [0]->wallets.create (source_id.prv));
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
		source->store.insert (transaction, key.prv);
	}
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_move");
    request_tree.put ("wallet", wallet_id.to_string ());
    request_tree.put ("source", source_id.prv.to_string ());
    boost::property_tree::ptree keys;
    boost::property_tree::ptree entry;
    entry.put ("", key.pub.to_string ());
    keys.push_back (std::make_pair ("", entry));
    request_tree.add_child ("accounts", keys);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    ASSERT_EQ ("1", response_tree.get <std::string> ("moved"));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_NE (destination->store.end (), destination->store.find (transaction, key.pub));
    ASSERT_NE (destination->store.end (), destination->store.find (transaction, rai::test_genesis_key.pub));
    ASSERT_EQ (source->store.end (), source->store.begin (transaction));
}

TEST (rpc, block)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.nodes [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "block");
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		request_tree.put ("hash", system.nodes [0]->ledger.latest (transaction, rai::genesis_account).to_string ());
	}
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
	auto contents (response_tree.get <std::string> ("contents"));
    ASSERT_FALSE (contents.empty ());
}