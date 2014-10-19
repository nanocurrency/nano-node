#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <rai/core/core.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

TEST (network, tcp_connection)
{
    boost::asio::io_service service;
    boost::asio::ip::tcp::acceptor acceptor (service);
    boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 24000);
    acceptor.open (endpoint.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (endpoint);
    acceptor.listen ();
    boost::asio::ip::tcp::socket incoming (service);
    auto done1 (false);
    std::string message1;
    acceptor.async_accept (incoming, 
       [&done1, &message1] (boost::system::error_code const & ec_a)
       {
           if (ec_a)
           {
               message1 = ec_a.message ();
               std::cerr << message1;
           }
           done1 = true;}
       );
    boost::asio::ip::tcp::socket connector (service);
    auto done2 (false);
    std::string message2;
    connector.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 24000), 
        [&done2, &message2] (boost::system::error_code const & ec_a)
        {
            if (ec_a)
            {
                message2 = ec_a.message ();
                std::cerr << message2;
            }
            done2 = true;
        });
    while (!done1 || !done2)
    {
        service.poll_one ();
    }
    ASSERT_EQ (0, message1.size ());
    ASSERT_EQ (0, message2.size ());
}

TEST (network, construction)
{
    rai::system system (24000, 1);
    ASSERT_EQ (1, system.clients.size ());
    ASSERT_EQ (24000, system.clients [0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
    rai::system system (24000, 1);
	system.clients [0]->network.remote = system.clients [0]->network.endpoint ();
	ASSERT_EQ (0, system.clients [0]->network.bad_sender_count);
	system.clients [0]->network.receive_action (boost::system::error_code {}, 0);
	ASSERT_EQ (1, system.clients [0]->network.bad_sender_count);
}

TEST (peer_container, empty_peers)
{
    rai::peer_container peers (rai::endpoint {});
    auto list (peers.purge_list (std::chrono::system_clock::now ()));
    ASSERT_EQ (0, list.size ());
}

TEST (peer_container, no_recontact)
{
    rai::peer_container peers (rai::endpoint {});
	rai::endpoint endpoint1 (boost::asio::ip::address_v4 (0x7f000001), 10000);
	ASSERT_EQ (0, peers.size ());
	ASSERT_FALSE (peers.contacting_peer (endpoint1));
	ASSERT_EQ (1, peers.size ());
	ASSERT_TRUE (peers.contacting_peer (endpoint1));
}

TEST (peer_container, no_self_incoming)
{
	rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::peer_container peers (self);
	peers.incoming_from_peer (self);
	ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, no_self_contacting)
{
	rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    rai::peer_container peers (self);
	peers.contacting_peer (self);
	ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, old_known)
{
	rai::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
	rai::endpoint other (boost::asio::ip::address_v4 (0x7f000001), 10001);
    rai::peer_container peers (self);
	peers.contacting_peer (other);
	ASSERT_FALSE (peers.known_peer (other));
    peers.incoming_from_peer (other);
    ASSERT_TRUE (peers.known_peer (other));
}

TEST (keepalive_req, deserialize)
{
    rai::keepalive_req message1;
    rai::endpoint endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    message1.peers [0] = endpoint;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message1.serialize (stream);
    }
    rai::keepalive_req message2;
    rai::bufferstream stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (message2.deserialize (stream));
    ASSERT_EQ (message1.peers, message2.peers);
}

TEST (keepalive_ack, deserialize)
{
    rai::keepalive_ack message1;
    rai::endpoint endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    message1.peers [0] = endpoint;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message1.serialize (stream);
    }
    rai::keepalive_ack message2;
    rai::bufferstream stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (message2.deserialize (stream));
    ASSERT_EQ (message1.peers, message2.peers);
}

TEST (peer_container, reserved_peers_no_contact)
{
    rai::peer_container peers (rai::endpoint {});
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0x00000001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xc0000201), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xc6336401), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xcb007101), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xe9fc0001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xf0000001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (rai::endpoint (boost::asio::ip::address_v4 (0xffffffff), 10000)));
	ASSERT_EQ (0, peers.size ());
}

TEST (peer_container, split)
{
    rai::peer_container peers (rai::endpoint {});
    auto now (std::chrono::system_clock::now ());
    rai::endpoint endpoint1 (boost::asio::ip::address_v4::any (), 100);
    rai::endpoint endpoint2 (boost::asio::ip::address_v4::any (), 101);
    peers.peers.insert ({endpoint1, now - std::chrono::seconds (1), now - std::chrono::seconds (1)});
    peers.peers.insert ({endpoint2, now + std::chrono::seconds (1), now + std::chrono::seconds (1)});
    auto list (peers.purge_list (now));
    ASSERT_EQ (1, list.size ());
    ASSERT_EQ (endpoint2, list [0].endpoint);
}

TEST (peer_container, fill_random_clear)
{
    rai::peer_container peers (rai::endpoint {});
    std::array <rai::endpoint, 24> target;
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v4 (0), 0); }));
}

TEST (peer_container, fill_random_full)
{
    rai::peer_container peers (rai::endpoint {});
    for (auto i (0); i < 100; ++i)
    {
        peers.incoming_from_peer (rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), i));
    }
    std::array <rai::endpoint, 24> target;
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000); }));
}

TEST (peer_container, fill_random_part)
{
    rai::peer_container peers (rai::endpoint {});
    for (auto i (0); i < 16; ++i)
    {
        peers.incoming_from_peer (rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), i + 1));
    }
    std::array <rai::endpoint, 24> target;
    std::fill (target.begin (), target.end (), rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + 16, [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000); }));
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + 16, [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 0); }));
    ASSERT_TRUE (std::all_of (target.begin () + 16, target.end (), [] (rai::endpoint const & endpoint_a) {return endpoint_a == rai::endpoint (boost::asio::ip::address_v4 (0), 0); }));
}

TEST (network, send_keepalive)
{
    rai::system system (24000, 2);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (1, list1.size ());
    while (list1 [0].last_contact == std::chrono::system_clock::now ());
    system.clients [0]->network.send_keepalive (system.clients [1]->network.endpoint ());
    auto initial (system.clients [0]->network.keepalive_ack_count);
    while (system.clients [0]->network.keepalive_ack_count == initial)
    {
        system.service->run_one ();
    }
    auto peers1 (system.clients [0]->peers.list ());
    auto peers2 (system.clients [1]->peers.list ());
    ASSERT_EQ (1, peers1.size ());
    ASSERT_EQ (1, peers2.size ());
    ASSERT_NE (peers1.end (), std::find_if (peers1.begin (), peers1.end (), [&system] (rai::peer_information const & information_a) {return information_a.endpoint == system.clients [1]->network.endpoint ();}));
    ASSERT_GT (peers1 [0].last_contact, list1 [0].last_contact);
    ASSERT_NE (peers2.end (), std::find_if (peers2.begin (), peers2.end (), [&system] (rai::peer_information const & information_a) {return information_a.endpoint == system.clients [0]->network.endpoint ();}));
}

TEST (network, multi_keepalive)
{
    rai::system system (24000, 1);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::client_init init1;
    rai::client client1 (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub);
    ASSERT_FALSE (init1.error ());
    client1.start ();
    client1.network.send_keepalive (system.clients [0]->network.endpoint ());
    ASSERT_EQ (0, client1.peers.size ());
    while (client1.peers.size () != 1 || system.clients [0]->peers.size () != 1)
    {
        system.service->run_one ();
    }
    rai::client_init init2;
    rai::client client2 (init2, system.service, 24002, system.processor, rai::test_genesis_key.pub);
    ASSERT_FALSE (init2.error ());
    client2.start ();
    client2.network.send_keepalive (system.clients [0]->network.endpoint ());
    while (client1.peers.size () != 2 || system.clients [0]->peers.size () != 2 || client2.peers.size () != 2)
    {
        system.service->run_one ();
    }
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <rai::send_block> (new rai::send_block));
    rai::keypair key1;
    rai::keypair key2;
    block->hashables.previous.clear ();
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    rai::publish_req req (std::move (block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        req.serialize (stream);
    }
    rai::publish_req req2;
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
    ASSERT_EQ (req.work, req2.work);
}

TEST (network, confirm_req)
{
    auto block (std::unique_ptr <rai::send_block> (new rai::send_block));
    rai::keypair key1;
    rai::keypair key2;
    block->hashables.previous.clear ();
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    rai::confirm_req req;
    req.block = std::move (block);
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        req.serialize (stream);
    }
    rai::confirm_req req2;
    rai::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
    ASSERT_EQ (req.work, req2.work);
}

TEST (network, send_discarded_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    block->hashables.previous.clear ();
    block->hashables.balance = 20;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block->hash (), block->signature);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
}

TEST (network, send_valid_publish)
{
    rai::system system (24000, 2);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    rai::send_block block2;
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    auto hash2 (block2.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash2, block2.signature);
    rai::frontier frontier2;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier2));
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (block2)), system.clients [0]->network.endpoint ());
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    rai::frontier frontier3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier3));
    ASSERT_FALSE (frontier2.hash == frontier3.hash);
    ASSERT_EQ (hash2, frontier3.hash);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (network, send_insufficient_work)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    block->hashables.previous.clear ();
    block->hashables.balance = 20;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block->hash (), block->signature);
    rai::publish_req publish;
    publish.block = std::move (block);
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        publish.serialize (stream);
    }
    auto client (system.clients [1]->shared ());
    system.clients [0]->network.send_buffer (bytes->data (), bytes->size (), system.clients [1]->network.endpoint (), [bytes, client] (boost::system::error_code const & ec, size_t size) {});
    ASSERT_EQ (0, system.clients [0]->network.insufficient_work_count);
    while (system.clients [1]->network.insufficient_work_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (1, system.clients [1]->network.insufficient_work_count);
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    rai::keypair key1;
    rai::confirm_ack con1;
    con1.vote.address = key1.pub;
    con1.vote.block = block1.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000), true);
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    rai::keypair key1;
    rai::confirm_ack con1;
    con1.vote.address = key1.pub;
    con1.vote.block = block1.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000), true);
}

TEST (receivable_processor, send_with_receive)
{
    auto amount (std::numeric_limits <rai::uint256_t>::max ());
    rai::system system (24000, 2);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    auto block1 (new rai::send_block ());
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->ledger.store.latest_get (rai::test_genesis_key.pub, frontier1));
    block1->hashables.previous = frontier1.hash;
    block1->hashables.balance = amount - 100;
    block1->hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1->hash (), block1->signature);
    ASSERT_EQ (amount, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (*block1));
    ASSERT_EQ (rai::process_result::progress, system.clients [1]->ledger.process (*block1));
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    system.clients [1]->conflicts.start (*block1, true);
    while (system.clients [0]->network.publish_req_count != 1)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [1]->ledger.account_balance (key2.pub));
}

TEST (rpc, account_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
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
    auto account_text (response_tree.get <std::string> ("account"));
    rai::uint256_union account;
    ASSERT_FALSE (account.decode_base58check (account_text));
    ASSERT_NE (system.clients [0]->wallet.end (), system.clients [0]->wallet.find (account));
}

TEST (rpc, account_balance)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("115792089237316195423570985008687907853269984665640564039457584007913129639935", balance_text);
}

TEST (rpc, wallet_contains)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", account);
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
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", account);
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

TEST (rpc, validate_account)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account");
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
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, validate_account_invalid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    account [0] ^= 0x1;
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account");
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
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.clients [0]->wallet.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "send");
    request_tree.put ("account", account);
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
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    rai::keypair key1;
    system.clients [0]->wallet.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "send");
    request_tree.put ("account", account);
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
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    rai::keypair key1;
    std::string key_text;
    key1.prv.encode_hex (key_text);
    system.clients [0]->wallet.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
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
    std::string account_text2;
    key1.pub.encode_base58check (account_text2);
    ASSERT_EQ (account_text1, account_text2);
}

TEST (network, receive_weight_change)
{
    rai::system system (24000, 2);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    system.clients [1]->representative = key2.pub;
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 2));
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <rai::client> const & client_a) {return client_a->ledger.weight (key2.pub) != 2;}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (rpc, wallet_list)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.clients [0]->wallet.insert (key2.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_list");
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
        ASSERT_NE (system.clients [0]->wallet.end (), system.clients [0]->wallet.find (*i));
    }
}

TEST (parse_endpoint, valid)
{
    std::string string ("127.0.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_FALSE (rai::parse_endpoint (string, endpoint));
    ASSERT_EQ (boost::asio::ip::address_v4::loopback (), endpoint.address ());
    ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
    std::string string ("127.0.0.1:24a00");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, nothing)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
    std::string string (":24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
    std::string string ("127.0.0.1:");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
    std::string string ("127.0.0.1");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (bootstrap_processor, process_none)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
}

TEST (bootstrap_processor, process_incomplete)
{
    rai::system system (24000, 1);
    auto initiator (std::make_shared <rai::bootstrap_initiator> (system.clients [0], [] () {}));
    initiator->requests.push (std::unique_ptr <rai::bulk_req> {});
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_req> request (new rai::bulk_req);
    request->start = rai::test_genesis_key.pub;
    request->end = genesis.hash ();
    auto bulk_req_initiator (std::make_shared <rai::bulk_req_initiator> (initiator, std::move (request)));
    rai::send_block block1;
    ASSERT_FALSE (bulk_req_initiator->process_block (block1));
    ASSERT_TRUE (bulk_req_initiator->process_end ());
}

TEST (bootstrap_processor, process_one)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (rai::test_genesis_key.pub, 100));
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    auto hash1 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    auto hash2 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
    auto hash3 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (hash1, hash3);
}

TEST (bootstrap_processor, process_two)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    auto hash1 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.clients [0]->transactions.send (rai::test_genesis_key.pub, 50));
    auto hash2 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.clients [0]->transactions.send (rai::test_genesis_key.pub, 50));
    auto hash3 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    ASSERT_NE (hash1, hash3);
    ASSERT_NE (hash2, hash3);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
    auto hash4 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (hash3, hash4);
}

TEST (bootstrap_processor, process_new)
{
    rai::system system (24000, 2);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    auto balance1 (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    auto balance2 (system.clients [0]->ledger.account_balance (key2.pub));
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24002, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [] () {});
    while (client1->ledger.account_balance (key2.pub) != balance2)
    {
        system.service->run_one ();
        system.processor.poll_one ();
    }
    ASSERT_EQ (balance1, client1->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (bulk_req, no_address)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req);
    req->start = 1;
    req->end = 2;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_req, genesis_to_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (system.clients [0]->ledger.latest (rai::test_genesis_key.pub), request->current);
    ASSERT_EQ (request->request->end, request->request->end);
}

TEST (bulk_req, no_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req {});
    req->start = rai::test_genesis_key.pub;
    req->end = 1;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_req, end_not_owned)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    rai::open_block open;
    open.hashables.representative = key2.pub;
    open.hashables.source = system.clients [0]->ledger.latest (rai::test_genesis_key.pub);
    rai::sign_message (key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (open));
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req {});
    req->start = key2.pub;
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_connection, none)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req {});
    req->start = genesis.hash ();
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_EQ (nullptr, block);
}

TEST (bulk_connection, get_next_on_open)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_req> req (new rai::bulk_req {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_NE (nullptr, block);
    ASSERT_TRUE (block->previous ().is_zero ());
    ASSERT_FALSE (connection->requests.empty ());
    ASSERT_FALSE (request->current.is_zero ());
    ASSERT_EQ (request->current, request->request->end);
}

TEST (frontier_req_response, destruction)
{
    {
        std::shared_ptr <rai::frontier_req_response> hold;
        {
            rai::system system (24000, 1);
            auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
            std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
            req->start.clear ();
            req->age = std::numeric_limits <decltype (req->age)>::max ();
            req->count = std::numeric_limits <decltype (req->count)>::max ();
            connection->requests.push (std::unique_ptr <rai::message> {});
            hold = std::make_shared <rai::frontier_req_response> (connection, std::move (req));
        }
    }
    ASSERT_TRUE (true);
}

TEST (frontier_req, begin)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (rai::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (rai::test_genesis_key.pub, pair.first);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (frontier_req, end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start = rai::test_genesis_key.pub.number () + 1;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_bound)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 0;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_cutoff)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 10;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (rai::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (rai::test_genesis_key.pub, pair.first);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (bulk, genesis)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::frontier frontier2;
    ASSERT_FALSE (client1->store.latest_get (rai::test_genesis_key.pub, frontier2));
    ASSERT_EQ (frontier1.hash, frontier2.hash);
    rai::keypair key2;
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    rai::frontier frontier3;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier3));
    ASSERT_NE (frontier1.hash, frontier3.hash);
    bool finished (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&finished] () {finished = true;});
    do
    {
        system.service->run_one ();
    } while (!finished);
    ASSERT_EQ (system.clients [0]->ledger.latest (rai::test_genesis_key.pub), client1->ledger.latest (rai::test_genesis_key.pub));
}

TEST (bulk, offline_send)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    } while (system.clients [0]->peers.empty () || client1->peers.empty ());
    rai::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    ASSERT_NE (std::numeric_limits <rai::uint256_t>::max (), system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    bool finished;
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&finished] () {finished = true;});
    do
    {
        system.service->run_one ();
        system.processor.poll_one ();
    } while (!finished || client1->ledger.account_balance (key2.pub) != 100);
	client1->stop ();
}