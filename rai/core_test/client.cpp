#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (client, stop)
{
    rai::system system (24000, 1);
    ASSERT_NE (system.clients [0]->wallets.items.end (), system.clients [0]->wallets.items.begin ());
    system.clients [0]->stop ();
    system.processor.run ();
    system.service->run ();
    ASSERT_TRUE (true);
}

TEST (client, block_store_path_failure)
{
    rai::client_init init;
    rai::processor_service processor;
    auto service (boost::make_shared <boost::asio::io_service> ());
    auto client (std::make_shared <rai::client> (init, service, 0, boost::filesystem::path {}, processor));
    client->stop ();
}

TEST (client, balance)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.wallet (0)->store.balance (system.clients [0]->ledger));
}

TEST (client, send_unkeyed)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (0)->store.password.value_set (rai::uint256_union (0));
    ASSERT_TRUE (system.wallet (0)->send (key2.pub, 1000));
}

TEST (client, send_self)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 1000));
    auto iterations (0);
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (client, send_single)
{
    rai::system system (24000, 2);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (client, send_single_observing_peer)
{
    rai::system system (24000, 3);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <rai::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (client, send_single_many_peers)
{
    rai::system system (24000, 10);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 1000));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1000, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_TRUE (system.clients [0]->ledger.account_balance (key2.pub).is_zero ());
    auto iterations (0);
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <rai::client> const & client_a) {return client_a->ledger.account_balance (key2.pub).is_zero();}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 2000);
    }
}

TEST (client, send_out_of_order)
{
    rai::system system (24000, 2);
    rai::keypair key2;
    rai::genesis genesis;
    rai::send_block send1;
    send1.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 1000;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    rai::send_block send2;
    send2.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 2000;
    send2.hashables.destination = key2.pub;
    send2.hashables.previous = send1.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send2)), rai::endpoint {});
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (send1)), rai::endpoint {});
    auto iterations (0);
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <rai::client> const & client_a) {return client_a->ledger.account_balance (rai::test_genesis_key.pub) != std::numeric_limits <rai::uint128_t>::max () - 2000;}))
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (client, bootstrap_end)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->start ();
    ASSERT_NE (nullptr, client1->processor.bootstrapped);
    ASSERT_EQ (0, client1->processor.bootstrapped->size ());
    for (auto i (0); i < rai::processor::bootstrap_max; ++i)
    {
        client1->processor.bootstrapped->insert (rai::endpoint (boost::asio::ip::address_v6::loopback (), 24002 + i));
    }
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (client1->processor.bootstrapped != nullptr);
    client1->stop ();
}

TEST (client, quick_confirm)
{
    rai::system system (24000, 1);
    rai::keypair key;
    system.wallet (0)->store.insert (key.prv);
    rai::send_block send;
    send.hashables.balance = 0;
    send.hashables.destination = key.pub;
    send.hashables.previous = system.clients [0]->ledger.latest (rai::test_genesis_key.pub);
    send.work = system.clients [0]->ledger.create_work (send);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->processor.process_receive (send));
    auto iterations (0);
    while (system.clients [0]->ledger.account_balance (key.pub).is_zero ())
    {
        system.processor.poll_one ();
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (client, auto_bootstrap)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    auto iterations1 (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    } while (system.clients [0]->ledger.account_balance (key2.pub) != 100);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    ASSERT_NE (nullptr, client1->processor.bootstrapped);
    ASSERT_EQ (0, client1->processor.bootstrapped->size ());
    ASSERT_NE (nullptr, system.clients [0]->processor.bootstrapped);
    ASSERT_EQ (0, system.clients [0]->processor.bootstrapped->size ());
    auto iterations2 (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    } while (client1->ledger.account_balance (key2.pub) != 100);
    ASSERT_NE (nullptr, client1->processor.bootstrapped);
    ASSERT_EQ (1, client1->processor.bootstrapped->size ());
    ASSERT_NE (client1->processor.bootstrapped->end (), client1->processor.bootstrapped->find (system.clients [0]->network.endpoint ()));
    ASSERT_NE (nullptr, system.clients [0]->processor.bootstrapped);
    ASSERT_EQ (1, system.clients [0]->processor.bootstrapped->size ());
    ASSERT_NE (system.clients [0]->processor.bootstrapped->end (), system.clients [0]->processor.bootstrapped->find (client1->network.endpoint ()));
    client1->stop ();
}

TEST (client, auto_bootstrap_reverse)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    rai::keypair key2;
    system.wallet (0)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    system.clients [0]->network.send_keepalive (client1->network.endpoint ());
    client1->start ();
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (client1->ledger.account_balance (key2.pub) != 100);
    client1->stop ();
}

TEST (client, multi_account_send_atomicness)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.wallet (0)->store.insert (key1.prv);
    system.wallet (0)->send (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2);
    system.wallet (0)->send (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2 + std::numeric_limits<rai::uint128_t>::max () / 4);
}

TEST (client, receive_gap)
{
    rai::system system (24000, 1);
    auto & client (*system.clients [0]);
    ASSERT_EQ (0, client.gap_cache.blocks.size ());
    rai::send_block block;
    rai::confirm_req message;
    message.block = block.clone ();
    client.processor.process_message (message, rai::endpoint {});
    ASSERT_EQ (1, client.gap_cache.blocks.size ());
}

TEST (client, scaling)
{
    rai::system system (24000, 1);
    auto max (std::numeric_limits <rai::uint128_t>::max ());
    auto down (rai::scale_down (max));
    auto up1 (rai::scale_up (down));
    auto up2 (rai::scale_up (down - 1));
    ASSERT_LT (up2, up1);
    ASSERT_EQ (up1 - up2, rai::scale_64bit_base10);
}

TEST (client, scale_num)
{
    rai::system system (24000, 1);
    rai::uint128_t num ("60000000000000000000000000000000000000");
    auto down (rai::scale_down (num));
    auto up (rai::scale_up (down));
    ASSERT_EQ (num, up);
}

TEST (client, merge_peers)
{
	rai::system system (24000, 1);
	std::array <rai::endpoint, 8> endpoints;
	endpoints.fill (rai::endpoint (boost::asio::ip::address_v6::loopback (), 24000));
	endpoints [0] = rai::endpoint (boost::asio::ip::address_v6::loopback (), 24001);
	system.clients [0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.clients [0]->peers.peers.size ());
}

TEST (client, search_pending)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    auto balance (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 1000));
    auto iterations1 (0);
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) == balance)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    system.wallet (0)->store.insert (key2.prv);
    system.clients [0]->processor.search_pending ();
    auto iterations2 (0);
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
}

TEST (client, connect_after_junk)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    uint64_t junk;
    client1->network.socket.async_send_to (boost::asio::buffer (&junk, sizeof (junk)), system.clients [0]->network.endpoint (), [] (boost::system::error_code const &, size_t) {});
    auto iterations1 (0);
    while (system.clients [0]->network.unknown_count == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    client1->start ();
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    auto iterations2 (0);
    while (client1->peers.empty ())
    {
        system.service->poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
    client1->stop ();
}