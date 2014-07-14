#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

TEST (network, construction)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    ASSERT_EQ (1, system.clients.size ());
    ASSERT_EQ (24000, system.clients [0]->network.socket.local_endpoint ().port ());
}

TEST (network, send_keepalive)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, 100);
    system.clients [0]->network.receive ();
    system.clients [1]->network.receive ();
    system.clients [0]->network.send_keepalive (system.endpoint (1));
    while (system.clients [0]->network.keepalive_ack_count == 0)
    {
        system.service->run_one ();
    }
    auto peers1 (system.clients [0]->peers.list ());
    auto peers2 (system.clients [1]->peers.list ());
    ASSERT_EQ (1, system.clients [0]->network.keepalive_ack_count);
    ASSERT_NE (peers1.end (), std::find (peers1.begin (), peers1.end (), system.endpoint (1)));
    ASSERT_EQ (1, system.clients [1]->network.keepalive_req_count);
    ASSERT_NE (peers2.end (), std::find (peers2.begin (), peers2.end (), system.endpoint (0)));
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <mu_coin::send_block> (new mu_coin::send_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->hashables.previous = 0;
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    mu_coin::publish_req req (std::move (block));
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        req.serialize (stream);
    }
    mu_coin::publish_req req2;
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (*req.block, *req2.block);
}

TEST (network, send_discarded_publish)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, 100);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    system.clients [0]->network.publish_block (system.endpoint (1), std::move (block));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (1, system.clients [1]->network.publish_req_count);
    ASSERT_EQ (0, system.clients [0]->network.publish_nak_count);
}

TEST (network, send_invalid_publish)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, 100);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->hashables.previous = 0;
    block->hashables.balance = 20;
    mu_coin::sign_message (key1.prv, key1.pub, block->hash (), block->signature);
    system.clients [0]->network.publish_block (system.endpoint (1), std::move (block));
    while (system.clients [0]->network.publish_ack_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (1, system.clients [1]->network.publish_req_count);
    ASSERT_EQ (1, system.clients [0]->network.publish_ack_count);
}

TEST (network, send_valid_publish)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, 100);
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    mu_coin::send_block block2;
    mu_coin::block_hash hash1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (key1.pub, hash1));
    block2.hashables.previous = hash1;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    auto hash2 (block2.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash2, block2.signature);
    mu_coin::block_hash hash3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (key1.pub, hash3));
    system.clients [0]->processor.publish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (block2)), system.endpoint (0));
    while (system.clients [0]->network.publish_ack_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (1, system.clients [0]->network.publish_ack_count);
    ASSERT_EQ (0, system.clients [1]->network.publish_ack_count);
    ASSERT_EQ (0, system.clients [0]->network.publish_req_count);
    ASSERT_EQ (1, system.clients [1]->network.publish_req_count);
    mu_coin::block_hash hash4;
    ASSERT_FALSE (system.clients [1]->store.latest_get (key1.pub, hash4));
    ASSERT_FALSE (hash3 == hash4);
    ASSERT_EQ (hash2, hash4);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (key1.pub));
}

TEST (receivable_processor, timeout)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    auto receivable (std::make_shared <mu_coin::receivable_processor> (nullptr, mu_coin::endpoint {}, *system.clients [0]));
    ASSERT_EQ (0, system.clients [0]->processor.publish_listener_size ());
    ASSERT_FALSE (receivable->complete);
    ASSERT_EQ (0, system.processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (1, system.processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (2, system.processor.size ());
}

TEST (receivable_processor, confirm_no_pos)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, mu_coin::endpoint {}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    mu_coin::vectorstream stream (bytes);
    con1.serialize (stream);
    ASSERT_LE (bytes.size (), system.clients [0]->network.buffer.size ());
    std::copy (bytes.data (), bytes.data () + bytes.size (), system.clients [0]->network.buffer.begin ());
    system.clients [0]->network.receive_action (boost::system::error_code {}, bytes.size ());
    ASSERT_TRUE (receivable->acknowledged.is_zero ());
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 1);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, mu_coin::endpoint {}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        con1.serialize (stream);
    }
    ASSERT_LE (bytes.size (), system.clients [0]->network.buffer.size ());
    std::copy (bytes.data (), bytes.data () + bytes.size (), system.clients [0]->network.buffer.begin ());
    system.clients [0]->network.receive_action (boost::system::error_code {}, bytes.size ());
    ASSERT_EQ (1, receivable->acknowledged);
    ASSERT_FALSE (receivable->complete);
    // Shared_from_this, local, timeout, callback
    ASSERT_EQ (4, receivable.use_count ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, std::numeric_limits<mu_coin::uint256_t>::max ());
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, mu_coin::endpoint {}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        con1.serialize (stream);
    }
    ASSERT_LE (bytes.size (), system.clients [0]->network.buffer.size ());
    std::copy (bytes.data (), bytes.data () + bytes.size (), system.clients [0]->network.buffer.begin ());
    system.clients [0]->network.receive_action (boost::system::error_code {}, bytes.size ());
    ASSERT_EQ (std::numeric_limits<mu_coin::uint256_t>::max (), receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}

TEST (receivable_processor, send_with_receive)
{
    mu_coin::keypair key1;
    auto amount (std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, amount);
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    auto block1 (new mu_coin::send_block ());
    mu_coin::block_hash previous;
    ASSERT_FALSE (system.clients [0]->ledger.store.latest_get (key1.pub, previous));
    block1->hashables.previous = previous;
    block1->hashables.balance = amount - 100;
    block1->hashables.destination = key2.pub;
    mu_coin::sign_message (key1.prv, key1.pub, block1->hash (), block1->signature);
    ASSERT_EQ (amount, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, system.clients [1]->ledger.account_balance (key1.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->ledger.process (*block1));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [1]->ledger.process (*block1));
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (key1.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::publish_req> {new mu_coin::publish_req {std::unique_ptr <mu_coin::block> {block1}}}, mu_coin::endpoint {}, *system.clients [1]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [1]->processor.publish_listener_size ());
    // Confirm_req, confirm_ack, publish_req, publish_ack
    while (system.clients [1]->network.publish_ack_count < 1)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_EQ (100, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (key1.pub));
    ASSERT_EQ (100, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}

TEST (client, send_self)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    system.clients [0]->wallet.insert (key2.pub, key2.prv, system.clients [0]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_FALSE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
}

TEST (client, send_single)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->run_one ();
    }
}

TEST (client, send_single_observing_peer)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 3, key1.pub, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->run_one ();
    }
}

TEST (client, send_single_many_peers)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 10, key1.pub, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (key1.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->run_one ();
    }
}

TEST (rpc, account_create)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    boost::network::http::server <mu_coin::rpc>::request request;
    boost::network::http::server <mu_coin::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_create");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    system.clients [0]->rpc (request, response);
    ASSERT_EQ (boost::network::http::server <mu_coin::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto account_text (response_tree.get <std::string> ("account"));
    mu_coin::uint256_union account;
    ASSERT_FALSE (account.decode_hex (account_text));
    ASSERT_NE (system.clients [0]->wallet.end (), system.clients [0]->wallet.find (account));
}

TEST (rpc, account_balance)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 10000);
    std::string account;
    key1.pub.encode_hex (account);
    boost::network::http::server <mu_coin::rpc>::request request;
    boost::network::http::server <mu_coin::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    system.clients [0]->rpc (request, response);
    ASSERT_EQ (boost::network::http::server <mu_coin::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("10000", balance_text);
}

TEST (rpc, wallet_contents)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    std::string account;
    key1.pub.encode_hex (account);
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    boost::network::http::server <mu_coin::rpc>::request request;
    boost::network::http::server <mu_coin::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    system.clients [0]->rpc (request, response);
    ASSERT_EQ (boost::network::http::server <mu_coin::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("exists"));
    ASSERT_EQ ("1", exists_text);
}

TEST (network, receive_weight_change)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 2, key1.pub, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    system.clients [1]->representative = key2.pub;
    system.clients [0]->send (key2.pub, 2, system.clients [0]->wallet.password);
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.weight (key2.pub) != 2;}))
    {
        system.service->run_one ();
    }
}

TEST (rpc, wallet_list)
{
    mu_coin::keypair key1;
    mu_coin::system system (1, 24000, 25000, 1, key1.pub, 100);
    std::string account;
    key1.pub.encode_hex (account);
    system.clients [0]->wallet.insert (key1.pub, key1.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key2.pub, key2.prv, system.clients [0]->wallet.password);
    boost::network::http::server <mu_coin::rpc>::request request;
    boost::network::http::server <mu_coin::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_list");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    system.clients [0]->rpc (request, response);
    ASSERT_EQ (boost::network::http::server <mu_coin::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto & accounts_node (response_tree.get_child ("accounts"));
    std::vector <mu_coin::uint256_union> accounts;
    for (auto i (accounts_node.begin ()), j (accounts_node.end ()); i != j; ++i)
    {
        auto account (i->second.get <std::string> (""));
        mu_coin::uint256_union number;
        ASSERT_FALSE (number.decode_hex (account));
        accounts.push_back (number);
    }
    ASSERT_EQ (2, accounts.size ());
    for (auto i (accounts.begin ()), j (accounts.end ()); i != j; ++i)
    {
        ASSERT_NE (system.clients [0]->wallet.end (), system.clients [0]->wallet.find (*i));
    }
}