#include <gtest/gtest.h>

#include <rai/node.hpp>
#include <rai/rpc.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/thread.hpp>

TEST (rpc, account_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    ASSERT_TRUE (system.wallet (0)->exists (account));
}

TEST (rpc, account_balance)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, account_weight)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::block_hash latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
    rai::change_block block (latest, key.pub, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (latest));
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->process (block).code);
	auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, wallet_contains)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    account [0] ^= 0x1;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
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

TEST (rpc, send)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    system.wallet (0)->insert (rai::test_genesis_key.prv);
	boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
	request_tree.put ("source", rai::test_genesis_key.pub.to_base58check ());
    request_tree.put ("destination", rai::test_genesis_key.pub.to_base58check ());
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

TEST (rpc, send_fail)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.nodes [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
	request_tree.put ("source", rai::test_genesis_key.pub.to_base58check ());
    request_tree.put ("destination", rai::test_genesis_key.pub.to_base58check ());
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    rai::keypair key1;
    std::string key_text;
    key1.prv.encode_hex (key_text);
	system.wallet (0)->insert (key1.prv);
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    rai::keypair key2;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
	system.wallet (0)->insert (key2.prv);
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
        ASSERT_TRUE (system.wallet (0)->exists (*i));
    }
}

TEST (rpc, wallet_key_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    system.wallet (0)->insert (rai::test_genesis_key.prv);
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
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
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
    rai::wallet_store store (error, transaction, rai::genesis_account, "0", wallet_json);
    ASSERT_FALSE (error);
    ASSERT_TRUE (store.exists (transaction, rai::test_genesis_key.pub));
}

TEST (rpc, wallet_destroy)
{
    rai::system system (24000, 1);
    auto wallet_id (system.nodes [0]->wallets.items.begin ()->first);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
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
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    auto destination (system.wallet (0));
    rai::keypair key;
	destination->insert (rai::test_genesis_key.prv);
    rai::keypair source_id;
    auto source (system.nodes [0]->wallets.create (source_id.prv));
	source->insert (key.prv);
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
    ASSERT_TRUE (destination->exists (key.pub));
    ASSERT_TRUE (destination->exists (rai::test_genesis_key.pub));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_EQ (source->store.end (), source->store.begin (transaction));
}

TEST (rpc, block)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "block");
	request_tree.put ("hash", system.nodes [0]->latest (rai::genesis_account).to_string ());
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

TEST (rpc, process_block)
{
    rai::system system (24000, 1);
	rai::keypair key;
	auto latest (system.nodes [0]->latest (rai::test_genesis_key.pub));
	rai::send_block send (latest, key.pub, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, system.work.generate (latest));
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "process");
	std::string json;
	send.serialize_json (json);
	request_tree.put ("block", json);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
	ASSERT_EQ (send.hash (), system.nodes [0]->latest (rai::test_genesis_key.pub));
}

TEST (rpc, price_free)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "price");
	request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
	request_tree.put ("amount", "1");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
	auto price (response_tree.get <std::string> ("price"));
	auto value (std::stoi (price));
	ASSERT_EQ (0, value);
}

TEST (rpc, price_amount_high)
{
    rai::system system (24000, 1);
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "price");
	request_tree.put ("account", key.pub.to_base58check ());
	request_tree.put ("amount", "1");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::bad_request, response.status);
}

TEST (rpc, price_bad)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "price");
	request_tree.put ("account", rai::test_genesis_key.pub.to_base58check ());
	request_tree.put ("amount", "1a");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::bad_request, response.status);
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
			source [key.pub] = key.prv;
			system.nodes [0]->store.account_put (transaction, key.pub, rai::account_info (key.prv, 0, 0, 0));
		}
	}
	rai::keypair key;
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "frontiers");
	request_tree.put ("account", rai::account (0).to_base58check ());
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto & frontiers_node (response_tree.get_child ("frontiers"));
    std::unordered_map <rai::account, rai::block_hash> frontiers;
    for (auto i (frontiers_node.begin ()), j (frontiers_node.end ()); i != j; ++i)
    {
        rai::account account;
		account.decode_base58check (i->first);
		rai::block_hash frontier;
		frontier.decode_hex (i->second.get <std::string> (""));
        frontiers [account] = frontier;
    }
	ASSERT_EQ (1, frontiers.erase (rai::test_genesis_key.pub));
	ASSERT_EQ (source, frontiers);
}

TEST (rpc_config, serialization)
{
	rai::rpc_config config1;
	config1.address = boost::asio::ip::address_v6::any();
	config1.port = 10;
	config1.enable_control = true;
	boost::property_tree::ptree tree;
	config1.serialize_json (tree);
	rai::rpc_config config2;
	ASSERT_NE (config2.address, config1.address);
	ASSERT_NE (config2.port, config1.port);
	ASSERT_NE (config2.enable_control, config1.enable_control);
	config2.deserialize_json (tree);
	ASSERT_EQ (config2.address, config1.address);
	ASSERT_EQ (config2.port, config1.port);
	ASSERT_EQ (config2.enable_control, config1.enable_control);
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
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "search_pending");
	request_tree.put ("wallet", wallet);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
	auto iterations (0);
	while (system.nodes [0]->balance (rai::test_genesis_key.pub) != rai::genesis_amount)
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}

TEST (rpc, keepalive)
{
    rai::system system (24000, 1);
	rai::node_init init1;
    auto node1 (std::make_shared <rai::node> (init1, system.service, 24001, rai::unique_path (), system.processor, system.logging, system.work));
    node1->start ();
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, *system.nodes [0], rai::rpc_config (true));
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "keepalive");
	request_tree.put ("address", boost::str (boost::format ("%1%") % node1->network.endpoint ().address ()));
	request_tree.put ("port", boost::str (boost::format ("%1%") % node1->network.endpoint ().port ()));
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
	auto iterations (0);
	ASSERT_FALSE (system.nodes [0]->peers.known_peer (node1->network.endpoint ()));
	while (!system.nodes [0]->peers.known_peer (node1->network.endpoint ()))
	{
		system.poll ();
		++iterations;
		ASSERT_LT (iterations, 200);
	}
}