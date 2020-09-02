#include <nano/node/election.hpp>
#include <nano/node/testing.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (election, construction)
{
	nano::system system (1);
	nano::genesis genesis;
	auto & node = *system.nodes[0];
	genesis.open->sideband_set (nano::block_sideband (nano::genesis_account, 0, nano::genesis_amount, 1, nano::seconds_since_epoch (), nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0));
	auto election = node.active.insert (genesis.open).election;
	election->transition_active ();
}

TEST (election, quorum_minimum_flip_success)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	std::shared_ptr<nano::block> send1 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta ())
	                                     .link (key1.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send1);
	nano::keypair key2;
	std::shared_ptr<nano::block> send2 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta ())
	                                     .link (key2.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send2);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.block_processor.flush ();
	auto election{ node1.active.insert (send1) };
	ASSERT_FALSE (election.inserted);
	ASSERT_NE (nullptr, election.election);
	ASSERT_EQ (2, election.election->blocks.size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, 1, send2));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send2->hash ()));
	ASSERT_TRUE (election.election->confirmed ());
}

TEST (election, quorum_minimum_flip_fail)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	std::shared_ptr<nano::block> send1 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta () - 1)
	                                     .link (key1.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send1);
	nano::keypair key2;
	std::shared_ptr<nano::block> send2 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta () - 1)
	                                     .link (key2.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send2);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.block_processor.flush ();
	auto election{ node1.active.insert (send1) };
	ASSERT_FALSE (election.inserted);
	ASSERT_NE (nullptr, election.election);
	ASSERT_EQ (2, election.election->blocks.size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, 1, send2));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_FALSE (election.election->confirmed ());
}

TEST (election, quorum_minimum_confirm_success)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	std::shared_ptr<nano::block> send1 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta ())
	                                     .link (key1.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	node1.block_processor.flush ();
	auto election{ node1.active.insert (send1) };
	ASSERT_FALSE (election.inserted);
	ASSERT_NE (nullptr, election.election);
	ASSERT_EQ (1, election.election->blocks.size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, 1, send1));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_TRUE (election.election->confirmed ());
}

TEST (election, quorum_minimum_confirm_fail)
{
	nano::system system;
	nano::node_config node_config (nano::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	std::shared_ptr<nano::block> send1 = builder.state ()
	                                     .account (nano::dev_genesis_key.pub)
	                                     .previous (nano::genesis_hash)
	                                     .representative (nano::dev_genesis_key.pub)
	                                     .balance (node1.online_reps.delta () - 1)
	                                     .link (key1.pub)
	                                     .work (0)
	                                     .sign (nano::dev_genesis_key.prv, nano::dev_genesis_key.pub)
	                                     .build ();
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	node1.block_processor.flush ();
	auto election{ node1.active.insert (send1) };
	ASSERT_FALSE (election.inserted);
	ASSERT_NE (nullptr, election.election);
	ASSERT_EQ (1, election.election->blocks.size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev_genesis_key.pub, nano::dev_genesis_key.prv, 1, send1));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_FALSE (election.election->confirmed ());
}
