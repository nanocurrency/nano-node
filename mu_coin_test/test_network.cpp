#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

TEST (network, construction)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    ASSERT_EQ (1, system.clients.size ());
    ASSERT_EQ (24000, system.clients [0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
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
	mu_coin::endpoint endpoint1 (boost::asio::ip::address_v4 (0x7f000001), 100000);
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
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0x00000001), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xc0000201), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xc6336401), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xcb007101), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xe9fc0001), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xf0000001), 100000)));
	ASSERT_TRUE (peers.contacting_peer (mu_coin::endpoint (boost::asio::ip::address_v4 (0xffffffff), 100000)));
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
    mu_coin::system system (1, 24000, 25000, 2, 100);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (1, list1.size ());
    system.clients [0]->network.send_keepalive (system.clients [1]->network.endpoint ());
    while (system.clients [0]->network.keepalive_ack_count == 0)
    {
        system.service->run_one ();
    }
    auto peers1 (system.clients [0]->peers.list ());
    auto peers2 (system.clients [1]->peers.list ());
    ASSERT_EQ (1, peers1.size ());
    ASSERT_EQ (1, peers2.size ());
    ASSERT_EQ (1, system.clients [0]->network.keepalive_ack_count);
    ASSERT_NE (peers1.end (), std::find_if (peers1.begin (), peers1.end (), [&system] (mu_coin::peer_information const & information_a) {return information_a.endpoint == system.clients [1]->network.endpoint ();}));
    ASSERT_GT (peers1 [0].last_contact, list1 [0].last_contact);
    ASSERT_EQ (1, system.clients [1]->network.keepalive_req_count);
    ASSERT_NE (peers2.end (), std::find_if (peers2.begin (), peers2.end (), [&system] (mu_coin::peer_information const & information_a) {return information_a.endpoint == system.clients [0]->network.endpoint ();}));
}

TEST (network, multi_keepalive)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, system.test_genesis_address.pub, system.genesis);
    client1.start ();
    client1.network.send_keepalive (system.clients [0]->network.endpoint ());
    ASSERT_EQ (0, client1.peers.size ());
    while (client1.peers.size () != 1 || system.clients [0]->peers.size () != 1)
    {
        system.service->run_one ();
    }
    mu_coin::client client2 (system.service, system.pool, 24002, 25002, system.processor, system.test_genesis_address.pub, system.genesis);
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
    mu_coin::system system (1, 24000, 25000, 2, 100);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    ASSERT_EQ (system.genesis.hash (), system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (system.genesis.hash (), system.clients [1]->ledger.latest (system.test_genesis_address.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (system.genesis.hash (), system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (system.genesis.hash (), system.clients [1]->ledger.latest (system.test_genesis_address.pub));
}

TEST (network, send_invalid_publish)
{
    mu_coin::system system (1, 24000, 25000, 2, 100);
    std::unique_ptr <mu_coin::send_block> block (new mu_coin::send_block);
    block->hashables.previous = 0;
    block->hashables.balance = 20;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, block->hash (), block->signature);
    system.clients [0]->network.publish_block (system.clients [1]->network.endpoint (), std::move (block));
    ASSERT_EQ (system.genesis.hash (), system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (system.genesis.hash (), system.clients [1]->ledger.latest (system.test_genesis_address.pub));
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (system.genesis.hash (), system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (system.genesis.hash (), system.clients [1]->ledger.latest (system.test_genesis_address.pub));
}

TEST (network, send_valid_publish)
{
    mu_coin::system system (1, 24000, 25000, 2, 100);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv, system.clients [1]->wallet.password);
    mu_coin::send_block block2;
    mu_coin::block_hash hash1;
    uint64_t time1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (system.test_genesis_address.pub, hash1, time1));
    block2.hashables.previous = hash1;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    auto hash2 (block2.hash ());
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, hash2, block2.signature);
    mu_coin::block_hash hash3;
    uint64_t time2;
    ASSERT_FALSE (system.clients [1]->store.latest_get (system.test_genesis_address.pub, hash3, time2));
    system.clients [0]->processor.process_and_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (block2)), system.clients [0]->network.endpoint ());
    while (system.clients [1]->network.publish_req_count == 0)
    {
        system.service->run_one ();
    }
    mu_coin::block_hash hash4;
    uint64_t time3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (system.test_genesis_address.pub, hash4, time3));
    ASSERT_FALSE (hash3 == hash4);
    ASSERT_EQ (hash2, hash4);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (system.test_genesis_address.pub));
}

TEST (receivable_processor, timeout)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto receivable (std::make_shared <mu_coin::receivable_processor> (nullptr, *system.clients [0]));
    ASSERT_EQ (0, system.clients [0]->processor.publish_listener_size ());
    ASSERT_FALSE (receivable->complete);
    ASSERT_EQ (1, system.processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (2, system.processor.size ());
    receivable->advance_timeout ();
    ASSERT_EQ (3, system.processor.size ());
}

TEST (receivable_processor, confirm_no_pos)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::block> {block1}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = system.test_genesis_address.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, con1.hash (), con1.signature);
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
    mu_coin::system system (1, 24000, 25000, 1, 1);
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::block> {block1}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = system.test_genesis_address.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        con1.serialize (stream);
    }
    ASSERT_LE (bytes.size (), system.clients [0]->network.buffer.size ());
    std::copy (bytes.data (), bytes.data () + bytes.size (), system.clients [0]->network.buffer.begin ());
    system.clients [0]->network.remote = mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    system.clients [0]->network.receive_action (boost::system::error_code {}, bytes.size ());
    ASSERT_EQ (1, receivable->acknowledged);
    ASSERT_FALSE (receivable->complete);
    // Shared_from_this, local, timeout, callback
    ASSERT_EQ (4, receivable.use_count ());
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits<mu_coin::uint256_t>::max ());
    auto block1 (new mu_coin::send_block ());
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::block> {block1}, *system.clients [0]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [0]->processor.publish_listener_size ());
    mu_coin::confirm_ack con1;
	con1.session = receivable->session;
    con1.address = system.test_genesis_address.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, con1.hash (), con1.signature);
    std::vector <uint8_t> bytes;
    {
        mu_coin::vectorstream stream (bytes);
        con1.serialize (stream);
    }
    ASSERT_LE (bytes.size (), system.clients [0]->network.buffer.size ());
    std::copy (bytes.data (), bytes.data () + bytes.size (), system.clients [0]->network.buffer.begin ());
    system.clients [0]->network.remote = mu_coin::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000);
    system.clients [0]->network.receive_action (boost::system::error_code {}, bytes.size ());
    ASSERT_EQ (std::numeric_limits<mu_coin::uint256_t>::max (), receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}

TEST (receivable_processor, send_with_receive)
{
    auto amount (std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::system system (1, 24000, 25000, 2, amount);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.pub, key2.prv, system.clients [1]->wallet.password);
    auto block1 (new mu_coin::send_block ());
    mu_coin::block_hash previous;
    uint64_t time;
    ASSERT_FALSE (system.clients [0]->ledger.store.latest_get (system.test_genesis_address.pub, previous, time));
    block1->hashables.previous = previous;
    block1->hashables.balance = amount - 100;
    block1->hashables.destination = key2.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, block1->hash (), block1->signature);
    ASSERT_EQ (amount, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, system.clients [1]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->ledger.process (*block1));
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [1]->ledger.process (*block1));
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::block> {block1}, *system.clients [1]));
    receivable->run ();
    ASSERT_EQ (1, system.clients [1]->processor.publish_listener_size ());
    while (system.clients [0]->network.publish_req_count != 1)
    {
        system.service->run_one ();
    }
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (100, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_EQ (100, system.clients [1]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, receivable->acknowledged);
    ASSERT_TRUE (receivable->complete);
    ASSERT_EQ (3, receivable.use_count ());
}

TEST (receivable_processor, timeout_after_acknowledge)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto receivable (std::make_shared <mu_coin::receivable_processor> (std::unique_ptr <mu_coin::block> (new mu_coin::send_block), *system.clients [0]));
    receivable->process_acknowledged (std::numeric_limits <mu_coin::uint256_t>::max ());
    receivable->timeout_action ();
}

TEST (client, send_self)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    system.clients [0]->wallet.insert (key2.prv, system.clients [0]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_FALSE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
}

TEST (client, send_single)
{
    mu_coin::system system (1, 24000, 25000, 2, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->run_one ();
    }
}

TEST (client, send_single_observing_peer)
{
    mu_coin::system system (1, 24000, 25000, 3, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->run_one ();
    }
}

TEST (client, send_single_many_peers)
{
    mu_coin::system system (1, 24000, 25000, 10, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    system.clients [1]->wallet.insert (key2.prv, system.clients [1]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 1000, system.clients [0]->wallet.password));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 1000, system.clients [0]->ledger.account_balance (system.test_genesis_address.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->run_one ();
    }
}

TEST (rpc, account_create)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
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
    mu_coin::system system (1, 24000, 25000, 1, 10000);
    std::string account;
    system.test_genesis_address.pub.encode_hex (account);
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
    mu_coin::system system (1, 24000, 25000, 1, 100);
    std::string account;
    system.test_genesis_address.pub.encode_hex (account);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
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
    mu_coin::system system (1, 24000, 25000, 2, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [1]->wallet.insert (key2.prv, system.clients [1]->wallet.password);
    system.clients [1]->representative = key2.pub;
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 2, system.clients [0]->wallet.password));
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.weight (key2.pub) != 2;}))
    {
        system.service->run_one ();
    }
}

TEST (rpc, wallet_list)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    std::string account;
    system.test_genesis_address.pub.encode_hex (account);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (key2.prv, system.clients [0]->wallet.password);
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

TEST (bootstrap_iterator, empty)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::bootstrap_iterator iterator (store);
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}

TEST (bootstrap_iterator, only_store)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address;
    mu_coin::block_hash hash;
    store.latest_put (address, hash, 100);
    mu_coin::bootstrap_iterator iterator (store);
    ++iterator;
    ASSERT_EQ (address, std::get <0> (iterator.current));
    ASSERT_EQ (hash, std::get <1> (iterator.current));
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}

TEST (bootstrap_iterator, only_observed)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::bootstrap_iterator iterator (store);
    mu_coin::address address;
    iterator.observed.insert (address);
    ++iterator;
    ASSERT_EQ (address, std::get <0> (iterator.current));
    ASSERT_TRUE (std::get <1> (iterator.current).is_zero ());
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}

TEST (bootstrap_iterator, observed_before_store)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::bootstrap_iterator iterator (store);
    mu_coin::address address1 (10);
    mu_coin::address address2 (100);
    iterator.observed.insert (address1);
    mu_coin::block_hash hash;
    store.latest_put (address2, hash, 100);
    ++iterator;
    ASSERT_EQ (address1, std::get <0> (iterator.current));
    ASSERT_TRUE (std::get <1> (iterator.current).is_zero ());
    ++iterator;
    ASSERT_EQ (address2, std::get <0> (iterator.current));
    ASSERT_EQ (hash, std::get <1> (iterator.current));
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}

TEST (bootstrap_iterator, store_before_observed)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::bootstrap_iterator iterator (store);
    mu_coin::address address1 (10);
    mu_coin::address address2 (100);
    mu_coin::block_hash hash;
    store.latest_put (address1, hash, 100);
    iterator.observed.insert (address2);
    ++iterator;
    ASSERT_EQ (address1, std::get <0> (iterator.current));
    ASSERT_EQ (hash, std::get <1> (iterator.current));
    ++iterator;
    ASSERT_EQ (address2, std::get <0> (iterator.current));
    ASSERT_TRUE (std::get <1> (iterator.current).is_zero ());
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}


TEST (bootstrap_iterator, observed_in_iteration)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::bootstrap_iterator iterator (store);
    mu_coin::address address1 (100);
    mu_coin::address address2 (10);
    mu_coin::block_hash hash;
    store.latest_put (address1, hash, 100);
    ++iterator;
    ASSERT_EQ (address1, std::get <0> (iterator.current));
    ASSERT_EQ (hash, std::get <1> (iterator.current));
    iterator.observed.insert (address2);
    ++iterator;
    ASSERT_EQ (address2, std::get <0> (iterator.current));
    ASSERT_TRUE (std::get <1> (iterator.current).is_zero ());
    ++iterator;
    ASSERT_TRUE (std::get <0> (iterator.current).is_zero ());
}

TEST (bootstrap_iterator, observe_send)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::address address;
    mu_coin::block_hash hash;
    store.latest_put (address, hash, 100);
    mu_coin::bootstrap_iterator iterator (store);
    mu_coin::send_block send;
    send.hashables.destination = 1;
    iterator.observed_block (send);
    ASSERT_EQ (1, iterator.observed.size ());
}

TEST (bootstrap_processor, process_none)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (*system.clients [0], [] () {}));
    ++initiator->iterator;
    ASSERT_EQ (system.test_genesis_address.pub, std::get <0> (initiator->iterator.current));
    ASSERT_EQ (system.genesis.hash (), std::get <1> (initiator->iterator.current));
    initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
    std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
    request->start = system.test_genesis_address.pub;
    request->end =system.genesis.hash ();
    auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    ASSERT_FALSE (bulk_req_initiator->process_end ());
    ASSERT_TRUE (initiator->requests.empty ());
}

TEST (bootstrap_processor, process_incomplete)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (*system.clients [0], [] () {}));
    initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
    std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
    request->start = system.test_genesis_address.pub;
    request->end = system.genesis.hash ();
    auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    mu_coin::send_block block1;
    ASSERT_FALSE (bulk_req_initiator->process_block (block1));
    ASSERT_TRUE (bulk_req_initiator->process_end ());
    while (system.service->poll ());
}

TEST (bootstrap_processor, process_one)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (system.test_genesis_address.pub, 100, system.clients [0]->wallet.password));
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, system.test_genesis_address.pub, system.genesis);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (client1, [] () {}));
    ++initiator->iterator;
	initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
	std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
	request->start = system.test_genesis_address.pub;
	request->end = system.genesis.hash ();
	auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    auto hash1 (system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    auto hash2 (client1.ledger.latest (system.test_genesis_address.pub));
    ASSERT_NE (hash1, hash2);
    auto block (system.clients [0]->ledger.store.block_get (hash1));
    ASSERT_NE (nullptr, block);
    ASSERT_FALSE (bulk_req_initiator->process_block (*block));
    ASSERT_FALSE (bulk_req_initiator->process_end ());
    auto hash3 (client1.ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (hash1, hash3);
    ASSERT_TRUE (initiator->requests.empty ());
}

TEST (bootstrap_processor, process_two)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    auto hash1 (system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_FALSE (system.clients [0]->send (system.test_genesis_address.pub, 50, system.clients [0]->wallet.password));
    auto hash2 (system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_FALSE (system.clients [0]->send (system.test_genesis_address.pub, 50, system.clients [0]->wallet.password));
    auto hash3 (system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    ASSERT_NE (hash1, hash2);
    ASSERT_NE (hash1, hash3);
    ASSERT_NE (hash2, hash3);
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, system.test_genesis_address.pub, system.genesis);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (client1, [] () {}));
    ++initiator->iterator;
	initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
	std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
	request->start = system.test_genesis_address.pub;
	request->end = system.genesis.hash ();
	auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    auto block2 (system.clients [0]->ledger.store.block_get (hash3));
    ASSERT_FALSE (bulk_req_initiator->process_block (*block2));
    auto block1 (system.clients [0]->ledger.store.block_get (hash2));
    ASSERT_FALSE (bulk_req_initiator->process_block (*block1));
    ASSERT_FALSE (bulk_req_initiator->process_end ());
    auto hash4 (client1.ledger.latest (system.test_genesis_address.pub));
    ASSERT_EQ (hash3, hash4);
    ASSERT_TRUE (initiator->requests.empty ());
}

TEST (bootstrap_processor, process_new)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::keypair key2;
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 100, system.clients [0]->wallet.password));
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, system.test_genesis_address.pub, system.genesis);
    auto initiator (std::make_shared <mu_coin::bootstrap_initiator> (client1, [] () {}));
    ++initiator->iterator;
	initiator->requests.push (std::unique_ptr <mu_coin::bulk_req> {});
	std::unique_ptr <mu_coin::bulk_req> request (new mu_coin::bulk_req);
	request->start = system.test_genesis_address.pub;
	request->end = system.genesis.hash ();
	auto bulk_req_initiator (std::make_shared <mu_coin::bulk_req_initiator> (initiator, std::move (request)));
    auto hash1 (system.clients [0]->ledger.latest (system.test_genesis_address.pub));
    auto block (system.clients [0]->ledger.store.block_get (hash1));
    ASSERT_NE (nullptr, block);
    ASSERT_FALSE (bulk_req_initiator->process_block (*block));
    ASSERT_FALSE (bulk_req_initiator->process_end ());
    ASSERT_TRUE (initiator->iterator.observed.empty ());
    ASSERT_FALSE (initiator->requests.empty ());
    while (system.service->poll ());
}

TEST (bulk_req, no_address)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
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
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = system.test_genesis_address.pub;
    req->end = 0;
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (system.clients [0]->ledger.latest (system.test_genesis_address.pub), request->current);
    ASSERT_EQ (request->request->end, request->request->end);
}

TEST (bulk_req, no_end)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = system.test_genesis_address.pub;
    req->end = 1;
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_req, end_not_owned)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    mu_coin::keypair key2;
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 100, system.clients [0]->wallet.password));
    mu_coin::open_block open;
    open.hashables.representative = key2.pub;
    open.hashables.source = system.clients [0]->ledger.latest (system.test_genesis_address.pub);
    mu_coin::sign_message (key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->ledger.process (open));
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = key2.pub;
    req->end = system.genesis.hash ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_connection, none)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = system.genesis.hash ();
    req->end = system.genesis.hash ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_EQ (nullptr, block);
}

TEST (bulk_connection, get_next_on_open)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::bulk_req> req (new mu_coin::bulk_req {});
    req->start = system.test_genesis_address.pub;
    req->end = 0;
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::bulk_req_response> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_NE (nullptr, block);
    ASSERT_TRUE (block->previous ().is_zero ());
    ASSERT_FALSE (connection->requests.empty ());
    ASSERT_FALSE (request->current.is_zero ());
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk, genesis)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    system.clients [0]->wallet.insert (system.test_genesis_address.prv, system.clients [0]->wallet.password);
    mu_coin::client client1 (system.service, system.pool, 24001, 25001, system.processor, system.test_genesis_address.pub, system.genesis);
    mu_coin::block_hash latest1;
    uint64_t time1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (system.test_genesis_address.pub, latest1, time1));
    mu_coin::block_hash latest3;
    uint64_t time2;
    ASSERT_FALSE (client1.store.latest_get (system.test_genesis_address.pub, latest3, time2));
    ASSERT_EQ (latest1, latest3);
    mu_coin::keypair key2;
    ASSERT_FALSE (system.clients [0]->send (key2.pub, 100, system.clients [0]->wallet.password));
    mu_coin::block_hash latest2;
    uint64_t time3;
    ASSERT_FALSE (system.clients [0]->store.latest_get (system.test_genesis_address.pub, latest2, time3));
    ASSERT_NE (latest1, latest2);
    bool finished (false);
    client1.processor.bootstrap (system.clients [0]->bootstrap.endpoint (), [&finished] () {finished = true;});
    do
    {
        system.service->run_one ();
    } while (!finished);
    ASSERT_EQ (system.clients [0]->ledger.latest (system.test_genesis_address.pub), client1.ledger.latest (system.test_genesis_address.pub));
}

TEST (client, send_out_of_order)
{
    mu_coin::system system (1, 24000, 25000, 2, std::numeric_limits <mu_coin::uint256_t>::max ());
    mu_coin::keypair key2;
    mu_coin::send_block send1;
    send1.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 1000;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = system.genesis.hash ();
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send1.hash (), send1.signature);
    mu_coin::send_block send2;
    send2.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 2000;
    send2.hashables.destination = key2.pub;
    send2.hashables.previous = send1.hash ();
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send2.hash (), send2.signature);
    system.clients [0]->processor.process_and_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (send2)), mu_coin::endpoint {});
    system.clients [0]->processor.process_and_republish (std::unique_ptr <mu_coin::block> (new mu_coin::send_block (send1)), mu_coin::endpoint {});
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::unique_ptr <mu_coin::client> const & client_a) {return client_a->ledger.account_balance (system.test_genesis_address.pub) != std::numeric_limits <mu_coin::uint256_t>::max () - 2000;}))
    {
        system.service->run_one ();
    }
}

TEST (frontier_req, begin)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start = 0;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client.ledger.store.latest_begin (system.test_genesis_address.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (system.test_genesis_address.pub, pair.first);
    ASSERT_EQ (system.genesis.hash (), pair.second);
}

TEST (frontier_req, end)
{
    mu_coin::system system (1, 24000, 25000, 1, 100);
    auto connection (std::make_shared <mu_coin::bootstrap_connection> (nullptr, *system.clients [0]));
    std::unique_ptr <mu_coin::frontier_req> req (new mu_coin::frontier_req);
    req->start = system.test_genesis_address.pub.number () + 1;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <mu_coin::message> {});
    auto request (std::make_shared <mu_coin::frontier_req_response> (connection, std::move (req)));
    ASSERT_EQ (connection->client.ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}