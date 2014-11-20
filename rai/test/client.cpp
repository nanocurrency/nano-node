#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (client, stop)
{
    rai::system system (24000, 1);
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
    auto client (std::make_shared <rai::client> (init, service, 0, boost::filesystem::path {}, processor, rai::account {}));
    client->stop ();
}

TEST (client, balance)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.clients [0]->balance ());
}

TEST (client, send_unkeyed)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    system.clients [0]->wallet.password.value_set (rai::uint256_union (0));
    ASSERT_TRUE (system.clients [0]->transactions.send (key2.pub, 1000));
}

TEST (client, send_self)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    system.clients [0]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
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
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
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
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
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
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    system.clients [1]->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 1000));
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

TEST (client, auto_bootstrap)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    rai::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    ASSERT_NE (nullptr, client1->processor.bootstrapped);
    ASSERT_EQ (0, client1->processor.bootstrapped->size ());
    ASSERT_NE (nullptr, system.clients [0]->processor.bootstrapped);
    ASSERT_EQ (0, system.clients [0]->processor.bootstrapped->size ());
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (client1->ledger.account_balance (key2.pub) != 100);
    ASSERT_NE (nullptr, client1->processor.bootstrapped);
    ASSERT_EQ (1, client1->processor.bootstrapped->size ());
    ASSERT_NE (client1->processor.bootstrapped->end (), client1->processor.bootstrapped->find (system.clients [0]->network.endpoint ()));
    ASSERT_NE (nullptr, system.clients [0]->processor.bootstrapped);
    ASSERT_EQ (1, system.clients [0]->processor.bootstrapped->size ());
    ASSERT_NE (system.clients [0]->processor.bootstrapped->end (), system.clients [0]->processor.bootstrapped->find (client1->network.endpoint ()));
    client1->stop ();
}

TEST (client, bootstrap_end)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
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

TEST (client, auto_bootstrap_reverse)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor, rai::test_genesis_key.pub));
    ASSERT_FALSE (init1.error ());
    rai::keypair key2;
    client1->wallet.insert (key2.prv);
    ASSERT_FALSE (system.clients [0]->transactions.send (key2.pub, 100));
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
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.clients [0]->wallet.insert (key1.prv);
    system.clients [0]->transactions.send (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2);
    system.clients [0]->transactions.send (key1.pub, std::numeric_limits<rai::uint128_t>::max () / 2 + std::numeric_limits<rai::uint128_t>::max () / 4);
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