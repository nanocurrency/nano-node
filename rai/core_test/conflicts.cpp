#include <gtest/gtest.h>
#include <rai/node.hpp>

TEST (conflicts, start_stop)
{
    rai::system system (24000, 1);
    auto & client1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_TRUE (client1.conflicts.no_conflict (send1.hashables.previous));
    client1.conflicts.start (send1, false);
    ASSERT_TRUE (client1.conflicts.no_conflict (send1.hashables.previous));
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    auto root1 (send1.root ());
    auto existing1 (client1.conflicts.roots.find (root1));
    ASSERT_NE (client1.conflicts.roots.end (), existing1);
    auto votes1 (existing1->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    client1.conflicts.stop (root1);
    ASSERT_EQ (0, client1.conflicts.roots.size ());
}

TEST (conflicts, add_existing)
{
    rai::system system (24000, 1);
    auto & client1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    client1.conflicts.start (send2, false);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    rai::vote vote1;
    vote1.account = key2.pub;
    vote1.sequence = 0;
    vote1.block = send2.clone ();
    rai::sign_message (key2.prv, key2.pub, vote1.hash (), vote1.signature);
    ASSERT_TRUE (client1.conflicts.no_conflict (send1.hashables.previous));
    client1.conflicts.update (vote1);
    ASSERT_FALSE (client1.conflicts.no_conflict (send1.hashables.previous));
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    auto votes1 (client1.conflicts.roots [send2.root ()]);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
    rai::system system (24000, 1);
    auto & client1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = send1.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send2));
    client1.conflicts.start (send2, false);
    ASSERT_EQ (2, client1.conflicts.roots.size ());
}