#include <gtest/gtest.h>
#include <rai/node.hpp>

TEST (conflicts, start_stop)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (send1).code);
    ASSERT_EQ (0, node1.conflicts.roots.size ());
    ASSERT_TRUE (node1.conflicts.no_conflict (send1.hashables.previous));
    node1.conflicts.start (send1, false);
    ASSERT_TRUE (node1.conflicts.no_conflict (send1.hashables.previous));
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    auto root1 (send1.root ());
    auto existing1 (node1.conflicts.roots.find (root1));
    ASSERT_NE (node1.conflicts.roots.end (), existing1);
    auto votes1 (existing1->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    node1.conflicts.stop (root1);
    ASSERT_EQ (0, node1.conflicts.roots.size ());
}

TEST (conflicts, add_existing)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (send1).code);
    node1.conflicts.start (send1, false);
    rai::keypair key2;
    rai::send_block send2 (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    node1.conflicts.start (send2, false);
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    rai::vote vote1 (key2.pub, key2.prv, 0, send2.clone ());
    ASSERT_TRUE (node1.conflicts.no_conflict (send1.hashables.previous));
    node1.conflicts.update (vote1);
    ASSERT_FALSE (node1.conflicts.no_conflict (send1.hashables.previous));
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    auto votes1 (node1.conflicts.roots [send2.root ()]);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (send1).code);
    node1.conflicts.start (send1, false);
    rai::keypair key2;
    rai::send_block send2 (key2.pub, send1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, node1.process (send2).code);
    node1.conflicts.start (send2, false);
    ASSERT_EQ (2, node1.conflicts.roots.size ());
}