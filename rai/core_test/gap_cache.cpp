#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (gap_cache, add_new)
{
    rai::system system (24000, 1);
    rai::gap_cache cache (*system.clients [0]);
    rai::send_block block1;
    cache.add (rai::send_block (block1), block1.previous ());
    ASSERT_NE (cache.blocks.end (), cache.blocks.find (block1.previous ()));
}

TEST (gap_cache, add_existing)
{
    rai::system system (24000, 1);
    rai::gap_cache cache (*system.clients [0]);
    rai::send_block block1;
    auto previous (block1.previous ());
    cache.add (block1, previous);
    auto existing1 (cache.blocks.find (previous));
    ASSERT_NE (cache.blocks.end (), existing1);
    auto arrival (existing1->arrival);
    while (arrival == std::chrono::system_clock::now ());
    cache.add (block1, previous);
    ASSERT_EQ (1, cache.blocks.size ());
    auto existing2 (cache.blocks.find (previous));
    ASSERT_NE (cache.blocks.end (), existing2);
    ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
    rai::system system (24000, 1);
    rai::gap_cache cache (*system.clients [0]);
    rai::send_block block1;
    block1.hashables.previous.clear ();
    auto previous1 (block1.previous ());
    cache.add (rai::send_block (block1), previous1);
    auto existing1 (cache.blocks.find (previous1));
    ASSERT_NE (cache.blocks.end (), existing1);
    auto arrival (existing1->arrival);
    while (std::chrono::system_clock::now () == arrival);
    rai::send_block block3;
    block3.hashables.previous = 42;
    auto previous2 (block3.previous ());
    cache.add (rai::send_block (block3), previous2);
    ASSERT_EQ (2, cache.blocks.size ());
    auto existing2 (cache.blocks.find (previous2));
    ASSERT_NE (cache.blocks.end (), existing2);
    ASSERT_GT (existing2->arrival, arrival);
    ASSERT_EQ (arrival, cache.blocks.get <1> ().begin ()->arrival);
}

TEST (gap_cache, limit)
{
    rai::system system (24000, 1);
    rai::gap_cache cache (*system.clients [0]);
    for (auto i (0); i < cache.max * 2; ++i)
    {
        rai::send_block block1;
        block1.hashables.previous = i;
        auto previous (block1.previous ());
        cache.add (rai::send_block (block1), previous);
    }
    ASSERT_EQ (cache.max, cache.blocks.size ());
}

TEST (gap_cache, gap_bootstrap)
{
    rai::system system (24000, 2);
    auto iterations1 (0);
    while (system.clients [0]->bootstrap_initiator.in_progress || system.clients [1]->bootstrap_initiator.in_progress)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    rai::keypair key;
    rai::send_block send;
    send.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 100;
    send.hashables.destination = key.pub;
    send.hashables.previous = system.clients [0]->ledger.latest (rai::test_genesis_key.pub);
    send.work = system.clients [0]->create_work (send);
    std::string hash;
    send.hash ().encode_hex (hash);
    std::cerr << hash << std::endl;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send.hash (), send.signature);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->processor.process_receive (send));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 100, system.clients [0]->ledger.account_balance (rai::genesis_account));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.clients [1]->ledger.account_balance (rai::genesis_account));
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    system.wallet (0)->store.insert (key.prv);
    system.wallet (0)->send (key.pub, 100);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, system.clients [0]->ledger.account_balance (rai::genesis_account));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), system.clients [1]->ledger.account_balance (rai::genesis_account));
    auto iterations2 (0);
    while (system.clients [1]->ledger.account_balance (rai::genesis_account) != std::numeric_limits <rai::uint128_t>::max () - 200)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
}