#include <gtest/gtest.h>
#include <banano/node/testing.hpp>

TEST (conflicts, start_stop)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	rai::genesis genesis;
	rai::keypair key1;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	ASSERT_EQ (rai::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (0, node1.active.roots.size ());
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto root1 (send1->root ());
	auto existing1 (node1.active.roots.find (root1));
	ASSERT_NE (node1.active.roots.end (), existing1);
	auto votes1 (existing1->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
}

TEST (conflicts, add_existing)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	rai::genesis genesis;
	rai::keypair key1;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	ASSERT_EQ (rai::process_result::progress, node1.process (*send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	rai::keypair key2;
	auto send2 (std::make_shared<rai::send_block> (genesis.hash (), key2.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	node1.active.start (send2);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto vote1 (std::make_shared<rai::vote> (key2.pub, key2.prv, 0, send2));
	node1.active.vote (vote1);
	ASSERT_EQ (1, node1.active.roots.size ());
	auto votes1 (node1.active.roots.find (send2->root ())->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
	rai::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	rai::genesis genesis;
	rai::keypair key1;
	auto send1 (std::make_shared<rai::send_block> (genesis.hash (), key1.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	ASSERT_EQ (rai::process_result::progress, node1.process (*send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	rai::keypair key2;
	auto send2 (std::make_shared<rai::send_block> (send1->hash (), key2.pub, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
	ASSERT_EQ (rai::process_result::progress, node1.process (*send2).code);
	node1.active.start (send2);
	ASSERT_EQ (2, node1.active.roots.size ());
}
