#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>
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
    mu_coin::system system (1, 24000, 25000, 1);
    ASSERT_EQ (1, system.clients.size ());
    ASSERT_EQ (24000, system.clients [0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
    mu_coin::system system (1, 24000, 25000, 1);
	system.clients [0]->network.remote = system.clients [0]->network.endpoint ();
	ASSERT_EQ (0, system.clients [0]->network.bad_sender_count);
	system.clients [0]->network.receive_action (boost::system::error_code {}, 0);
	ASSERT_EQ (1, system.clients [0]->network.bad_sender_count);
}

TEST (peer_container, empty_peers)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
    auto list (peers.purge_list (std::chrono::system_clock::now ()));
    ASSERT_EQ (0, list.size ());
}

TEST (peer_container, no_recontact)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
	mu_coin::endpoint endpoint1 (boost::asio::ip::address_v4 (0x7f000001), 10000);
	ASSERT_EQ (0, peers.size ());
	ASSERT_FALSE (peers.contacting_peer (endpoint1));
	ASSERT_EQ (1, peers.size ());
	ASSERT_TRUE (peers.contacting_peer (endpoint1));
}

TEST (peer_container, no_self_incoming)
{
	mu_coin::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    mu_coin::peer_container peers (self);
	peers.incoming_from_peer (self);
	ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, no_self_contacting)
{
	mu_coin::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
    mu_coin::peer_container peers (self);
	peers.contacting_peer (self);
	ASSERT_TRUE (peers.peers.empty ());
}

TEST (peer_container, old_known)
{
	mu_coin::endpoint self (boost::asio::ip::address_v4 (0x7f000001), 10000);
	mu_coin::endpoint other (boost::asio::ip::address_v4 (0x7f000001), 10001);
    mu_coin::peer_container peers (self);
	peers.contacting_peer (other);
	ASSERT_FALSE (peers.known_peer (other));
    peers.incoming_from_peer (other);
    ASSERT_TRUE (peers.known_peer (other));
}

TEST (keepalive_req, deserialize)
{
    mu_coin::keepalive_req message1;
    mu_coin::endpoint endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    message1.peers [0] = endpoint;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        message1.serialize (stream);
    }
    mu_coin::keepalive_req message2;
    mu_coin::bufferstream stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (message2.deserialize (stream));
    ASSERT_EQ (message1.peers, message2.peers);
}

TEST (keepalive_ack, deserialize)
{
    mu_coin::keepalive_ack message1;
    mu_coin::endpoint endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    message1.peers [0] = endpoint;
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        message1.serialize (stream);
    }
    mu_coin::keepalive_ack message2;
    mu_coin::bufferstream stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (message2.deserialize (stream));
    ASSERT_EQ (message1.peers, message2.peers);
}

TEST (peer_container, reserved_peers_no_contact)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0x00000001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xc0000201), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xc6336401), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xcb007101), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xe9fc0001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xf0000001), 10000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xffffffff), 10000)));
	ASSERT_EQ (0, peers.size ());
}

TEST (peer_container, split)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
    auto now (std::chrono::system_clock::now ());
    mu_coin::endpoint endpoint1 (boost::asio::ip::address_v4::any (), 100);
    mu_coin::endpoint endpoint2 (boost::asio::ip::address_v4::any (), 101);
    peers.peers.insert ({endpoint1, now - std::chrono::seconds (1), now - std::chrono::seconds (1)});
    peers.peers.insert ({endpoint2, now + std::chrono::seconds (1), now + std::chrono::seconds (1)});
    auto list (peers.purge_list (now));
    ASSERT_EQ (1, list.size ());
    ASSERT_EQ (endpoint2, list [0].endpoint);
}

TEST (peer_container, fill_random_clear)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
    std::array <mu_coin::endpoint, 24> target;
    std::fill (target.begin (), target.end (), mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (mu_coin::endpoint const & endpoint_a) {return endpoint_a == mu_coin::endpoint (boost::asio::ip::address_v4 (0), 0); }));
}

TEST (peer_container, fill_random_full)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
    for (auto i (0); i < 100; ++i)
    {
        peers.incoming_from_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), i));
    }
    std::array <mu_coin::endpoint, 24> target;
    std::fill (target.begin (), target.end (), mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.end (), [] (mu_coin::endpoint const & endpoint_a) {return endpoint_a == mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000); }));
}

TEST (peer_container, fill_random_part)
{
    mu_coin::peer_container peers (mu_coin::endpoint {});
    for (auto i (0); i < 16; ++i)
    {
        peers.incoming_from_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), i + 1));
    }
    std::array <mu_coin::endpoint, 24> target;
    std::fill (target.begin (), target.end (), mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
    peers.random_fill (target);
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + 16, [] (mu_coin::endpoint const & endpoint_a) {return endpoint_a == mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000); }));
    ASSERT_TRUE (std::none_of (target.begin (), target.begin () + 16, [] (mu_coin::endpoint const & endpoint_a) {return endpoint_a == mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 0); }));
    ASSERT_TRUE (std::all_of (target.begin () + 16, target.end (), [] (mu_coin::endpoint const & endpoint_a) {return endpoint_a == mu_coin::endpoint (boost::asio::ip::address_v4 (0), 0); }));
}

TEST (network, send_keepalive)
{
    mu_coin::system system (1, 24000, 25000, 2);
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
    ASSERT_NE (peers1.end (), std::find_if (peers1.begin (), peers1.end (), [&system] (mu_coin::peer_information const & information_a) {return information_a.endpoint == system.clients [1]->network.endpoint ();}));
    ASSERT_GT (peers1 [0].last_contact, list1 [0].last_contact);
    ASSERT_NE (peers2.end (), std::find_if (peers2.begin (), peers2.end (), [&system] (mu_coin::peer_information const & information_a) {return information_a.endpoint == system.clients [0]->network.endpoint ();}));
}

TEST (network, multi_keepalive)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub);
    client1.start ();
    client1.network.send_keepalive (system.clients [0]->network.endpoint ());
    ASSERT_EQ (0, client1.peers.size ());
    while (client1.peers.size () != 1 || system.clients [0]->peers.size () != 1)
    {
        system.service->run_one ();
    }
    mu_coin::client client2 (system.service, system.pool, 24002, 25002, system.processor, mu_coin::test_genesis_key.pub);
    client2.start ();
    client2.network.send_keepalive (system.clients [0]->network.endpoint ());
    while (client1.peers.size () != 2 || system.clients [0]->peers.size () != 2 || client2.peers.size () != 2)
    {
        system.service->run_one ();
    }
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <mu_coin::send_block> (new mu_coin::send_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->hashables.previous.clear ();
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
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
    ASSERT_EQ (req.work, req2.work);
}

TEST (network, confirm_req)
{
    auto block (std::unique_ptr <mu_coin::send_block> (new mu_coin::send_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->hashables.previous.clear ();
    block->hashables.balance = 200;
    block->hashables.destination = key2.pub;
    mu_coin::confirm_req req;
    req.block = std::move (block);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        req.serialize (stream);
    }
    mu_coin::confirm_req req2;
    mu_coin::bufferstream stream2 (bytes.data (), bytes.size ());
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (req, req2);
    ASSERT_EQ (*req.block, *req2.block);
    ASSERT_EQ (req.work, req2.work);
}

TEST (network, send_discarded_publish)
{
    mu_coin::system system (1, 24000, 25000, 2);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    mu_coin::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (mu_coin::test_genesis_key.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (mu_coin::test_genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
    mu_coin::system system (1, 24000, 25000, 2);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->hashables.previous.clear ();
    block->hashables.balance = 20;
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, block->hash (), block->signature);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    mu_coin::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (mu_coin::test_genesis_key.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (mu_coin::test_genesis_key.pub));
}

TEST (network, send_valid_publish)
{
    mu_coin::system system (1, 24000, 25000, 2);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    mu_coin::send_block block2;
    mu_coin::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (mu_coin::test_genesis_key.pub, frontier1));
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    auto hash2 (block2.hash ());
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, hash2, block2.signature);
    mu_coin::frontier frontier2;
    ASSERT_FALSE (system.clients [1]->store.latest_get (mu_coin::test_genesis_key.pub, frontier2));
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (block2)), system.clients [0]->network.endpoint ());
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    mu_coin::frontier frontier3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (mu_coin::test_genesis_key.pub, frontier3));
    ASSERT_FALSE (frontier2.hash == frontier3.hash);
    ASSERT_EQ (hash2, frontier3.hash);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (mu_coin::test_genesis_key.pub));
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto & client1 (*system.clients [0]);
    mu_coin::genesis genesis;
    mu_coin::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    mu_coin::keypair key1;
    mu_coin::confirm_ack con1;
    con1.vote.address = key1.pub;
    con1.vote.block = block1.clone ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000), true);
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto & client1 (*system.clients [0]);
    mu_coin::genesis genesis;
    mu_coin::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    mu_coin::keypair key1;
    mu_coin::confirm_ack con1;
    con1.vote.address = key1.pub;
    con1.vote.block = block1.clone ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000), true);
}

TEST (receivable_processor, send_with_receive)
{
    auto amount (std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::system system (1, 24000, 25000, 2);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    auto block1 (new mu_coin::send_block ());
    mu_coin::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->ledger.store.latest_get (mu_coin::test_genesis_key.pub, frontier1));
    block1->hashables.previous = frontier1.hash;
    block1->hashables.balance = amount - 100;
    block1->hashables.destination = key2.pub;
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, block1->hash (), block1->signature);
    ASSERT_EQ (amount, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, system.clients [1]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->ledger.process (*block1));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [1]->ledger.process (*block1));
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    system.clients [1]->conflicts.start (*block1, true);
    while (system.clients [0]->network.publish_req_count != 1)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [1]->ledger.account_balance (key2.pub));
}

TEST (client, send_self)
{
    mu_coin::system system (1, 24000, 25000, 1);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    system.clients [0]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
}

TEST (client, send_single)
{
    mu_coin::system system (1, 24000, 25000, 2);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (client, send_single_observing_peer)
{
    mu_coin::system system (1, 24000, 25000, 3);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (client, send_single_many_peers)
{
    mu_coin::system system (1, 24000, 25000, 10);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (rpc, account_create)
{
    mu_coin::system system (1, 24000, 25000, 1);
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
    mu_coin::system system (1, 24000, 25000, 1);
    std::string account;
    mu_coin::test_genesis_key.pub.encode_hex (account);
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
    ASSERT_EQ ("115792089237316195423570985008687907853269984665640564039457584007913129639935", balance_text);
}

TEST (rpc, wallet_contents)
{
    mu_coin::system system (1, 24000, 25000, 1);
    std::string account;
    mu_coin::test_genesis_key.pub.encode_hex (account);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
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
    mu_coin::system system (1, 24000, 25000, 2);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    system.clients [1]->representative = key2.pub;
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 2));
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <mu_coin::client> const & client_a) {return client_a->ledger.weight (key2.pub) != 2;}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (rpc, wallet_list)
{
    mu_coin::system system (1, 24000, 25000, 1);
    std::string account;
    mu_coin::test_genesis_key.pub.encode_hex (account);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key2.prv);
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

TEST (parse_endpoint, valid)
{
    std::string string ("127.0.0.1:24000");
    mu_coin::endpoint endpoint;
    ASSERT_FALSE (mu_coin::parse_endpoint (string, endpoint));
    ASSERT_EQ (boost::asio::ip::address_v4::loopback (), endpoint.address ());
    ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
    std::string string ("127.0.0.1:24a00");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
    std::string string ("127.0q.0.1:24000");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, nothing)
{
    std::string string ("127.0q.0.1:24000");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
    std::string string (":24000");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
    std::string string ("127.0.0.1:");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
    std::string string ("127.0.0.1");
    mu_coin::endpoint endpoint;
    ASSERT_TRUE (mu_coin::parse_endpoint (string, endpoint));
}

TEST (bootstrap_processor, process_none)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
}

TEST (bootstrap_processor, process_incomplete)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (system.clients [0], [] () {}));
    initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
    mu_coin::genesis genesis;
    std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
    request->start = mu_coin::test_genesis_key.pub;
    request->end = genesis.hash ();
    auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    mu_coin::send_block block1;
    ASSERT_FALSE (bulk_req_initiator->process_block (block1));
    ASSERT_TRUE (bulk_req_initiator->process_end ());
}

TEST (bootstrap_processor, process_one)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (mu_coin::test_genesis_key.pub, 100));
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    auto hash1 (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    auto hash2 (client1->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
    auto hash3 (client1->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (hash1, hash3);
}

TEST (bootstrap_processor, process_two)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    auto hash1 (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_FALSE (system.clients [0]->transactions.send (mu_coin::test_genesis_key.pub, 50));
    auto hash2 (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_FALSE (system.clients [0]->transactions.send (mu_coin::test_genesis_key.pub, 50));
    auto hash3 (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    ASSERT_NE (hash1, hash3);
    ASSERT_NE (hash2, hash3);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&done] () {done = true;});
    while (!done)
    {
        system.service->run_one ();
    }
    auto hash4 (client1->ledger.latest (mu_coin::test_genesis_key.pub));
    ASSERT_EQ (hash3, hash4);
}

TEST (bootstrap_processor, process_new)
{
    mu_coin::system system (1, 24000, 25000, 2);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    auto balance1 (system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    auto balance2 (system.clients [0]->ledger.account_balance (key2.pub));
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24002, 25002, system.processor, mu_coin::test_genesis_key.pub));
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [] () {});
    while (client1->ledger.account_balance (key2.pub) != balance2)
    {
        system.service->run_one ();
        system.processor.poll_one ();
    }
    ASSERT_EQ (balance1, client1->ledger.account_balance (mu_coin::test_genesis_key.pub));
}

TEST (bulk_req, no_address)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req);
    req->start = 1;
    req->end = 2;
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_req, genesis_to_end)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = mu_coin::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub), request->current);
    ASSERT_EQ (request->request->end, request->request->end);
}

TEST (bulk_req, no_end)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = mu_coin::test_genesis_key.pub;
    req->end = 1;
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_req, end_not_owned)
{
    mu_coin::system system (1, 24000, 25000, 1);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    mu_coin::open_block open;
    open.hashables.representative = key2.pub;
    open.hashables.source = system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub);
    mu_coin::sign_message (key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->ledger.process (open));
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    mu_coin::genesis genesis;
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = key2.pub;
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_connection, none)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    mu_coin::genesis genesis;
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = genesis.hash ();
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_EQ (nullptr, block);
}

TEST (bulk_connection, get_next_on_open)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = mu_coin::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_NE (nullptr, block);
    ASSERT_TRUE (block->previous ().is_zero ());
    ASSERT_FALSE (connection->requests.empty ());
    ASSERT_FALSE (request->current.is_zero ());
    ASSERT_EQ (request->current, request->request->end);
}

TEST (client, send_out_of_order)
{
    mu_coin::system system (1, 24000, 25000, 2);
    mu_coin::keypair key2;
    mu_coin::genesis genesis;
    mu_coin::send_block send1;
    send1.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 1000;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, send1.hash (), send1.signature);
    mu_coin::send_block send2;
    send2.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 2000;
    send2.hashables.destination = key2.pub;
    send2.hashables.previous = send1.hash ();
    mu_coin::sign_message (mu_coin::test_genesis_key.prv, mu_coin::test_genesis_key.pub, send2.hash (), send2.signature);
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (send2)), mu_coin::endpoint {});
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (send1)), mu_coin::endpoint {});
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (mu_coin::test_genesis_key.pub) != std::numeric_limits <mu_coin::uint256_t>::max () - 2000;}))
    {
        system.service->run_one ();
    }
}

TEST (frontier_req, begin)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start.clear ();
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (mu_coin::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (mu_coin::test_genesis_key.pub, pair.first);
    mu_coin::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (frontier_req, end)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start = mu_coin::test_genesis_key.pub.number () + 1;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_bound)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start.clear ();
    req->age = 0;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_cutoff)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start.clear ();
    req->age = 10;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (mu_coin::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (mu_coin::test_genesis_key.pub, pair.first);
    mu_coin::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (bulk, genesis)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    mu_coin::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (mu_coin::test_genesis_key.pub, frontier1));
    mu_coin::frontier frontier2;
    ASSERT_FALSE (client1->store.latest_get (mu_coin::test_genesis_key.pub, frontier2));
    ASSERT_EQ (frontier1.hash, frontier2.hash);
    mu_coin::keypair key2;
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    mu_coin::frontier frontier3;
    ASSERT_FALSE (system.clients [0]->store.latest_get (mu_coin::test_genesis_key.pub, frontier3));
    ASSERT_NE (frontier1.hash, frontier3.hash);
    bool finished (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&finished] () {finished = true;});
    do
    {
        system.service->run_one ();
    } while (!finished);
    ASSERT_EQ (system.clients [0]->ledger.latest (mu_coin::test_genesis_key.pub), client1->ledger.latest (mu_coin::test_genesis_key.pub));
}

TEST (bulk, offline_send)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    } while (system.clients [0]->peers.empty () || client1->peers.empty ());
    mu_coin::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    ASSERT_NE (std::numeric_limits <mu_coin::uint256_t>::max (), system.clients [0]->ledger.account_balance (mu_coin::test_genesis_key.pub));
    bool finished;
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&finished] () {finished = true;});
    do
    {
        system.service->run_one ();
        system.processor.poll_one ();
    } while (!finished || client1->ledger.account_balance (key2.pub) != 100);
}

TEST (client, auto_bootstrap)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->peers.incoming_from_peer (system.clients [0]->network.endpoint ());
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    client1->peers.incoming_from_peer (client1->network.endpoint ());
    mu_coin::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    } while (client1->ledger.account_balance (key2.pub) != 100);
}

TEST (client, auto_bootstrap_reverse)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->peers.incoming_from_peer (system.clients [0]->network.endpoint ());
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    auto client1 (std::make_shared <mu_coin::client> (system.service, system.pool, 24001, 25001, system.processor, mu_coin::test_genesis_key.pub));
    client1->peers.incoming_from_peer (client1->network.endpoint ());
    mu_coin::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    system.clients [0]->network.send_keepalive (client1->network.endpoint ());
    client1->start ();
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    } while (client1->ledger.account_balance (key2.pub) != 100);
}

TEST (client, multi_account_send_atomicness)
{
    mu_coin::system system (1, 24000, 25000, 1);
    system.clients [0]->wallet.insert (mu_coin::test_genesis_key.prv);
    mu_coin::keypair key1;
    system.clients [0]->wallet.insert (key1.prv);
    system.clients [0]->transactions.send (key1.pub, std::numeric_limits<mu_coin::uint256_t>::max () / 2);
    system.clients [0]->transactions.send (key1.pub, std::numeric_limits<mu_coin::uint256_t>::max () / 2 + std::numeric_limits<mu_coin::uint256_t>::max () / 4);
}

TEST (client, scaling)
{
    mu_coin::system system (1, 24000, 25000, 1);
    auto max (std::numeric_limits <mu_coin::uint256_t>::max ());
    auto down (system.clients [0]->scale_down (max));
    auto up1 (system.clients [0]->scale_up (down));
    auto up2 (system.clients [0]->scale_up (down - 1));
    ASSERT_LT (up2, up1);
    ASSERT_EQ (up1 - up2, system.clients [0]->scale);
}