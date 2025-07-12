#include "nano/node/election.hpp"

#include <nano/node/online_reps.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (online_reps, basic)
{
	nano::test::system system;
	auto & node1 = *system.add_node ();
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	auto vote (std::make_shared<nano::vote> ());
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.online_reps.observe (nano::dev::genesis_key.pub);
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.online ());
	// 1 minimum, 1 maximum
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	node1.online_reps.force_sample ();
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.trended ());
	node1.online_reps.clear ();
	// 2 minimum, 1 maximum
	node1.online_reps.force_sample ();
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
}

TEST (online_reps, rep_crawler)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::milliseconds_since_epoch (), 0, std::vector<nano::block_hash>{ nano::dev::genesis->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	// Without rep crawler
	node1.vote_processor.vote_blocking (vote, std::make_shared<nano::transport::fake::channel> (node1));
	ASSERT_EQ (0, node1.online_reps.online ());
	// After inserting to rep crawler
	auto channel = std::make_shared<nano::transport::fake::channel> (node1);
	node1.rep_crawler.force_query (nano::dev::genesis->hash (), channel);
	node1.vote_processor.vote_blocking (vote, channel);
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.online ());
}

TEST (online_reps, election)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	// Start election
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (nano::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	// Process vote for ongoing election
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::milliseconds_since_epoch (), 0, std::vector<nano::block_hash>{ send1->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.vote_processor.vote_blocking (vote, std::make_shared<nano::transport::fake::channel> (node1));
	ASSERT_EQ (nano::dev::constants.genesis_amount - nano::Knano_ratio, node1.online_reps.online ());
}

// Online reps should be able to observe remote representative
TEST (online_reps, observe)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());

	// Addd genesis representative
	auto & node_rep = *system.add_node ();
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);

	// The node should see that weight as online
	ASSERT_TIMELY_EQ (10s, node.online_reps.online (), nano::dev::constants.genesis_amount);
	ASSERT_ALWAYS_EQ (1s, node.online_reps.online (), nano::dev::constants.genesis_amount);
}

TEST (online_reps, observe_multiple)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());

	auto & node_rep1 = *system.add_node (); // key1
	auto & node_rep2 = *system.add_node (); // key2 & key3

	auto const weight_1 = nano::nano_ratio * 1000;
	auto const weight_2 = nano::nano_ratio * 1000000;
	auto const weight_3 = nano::nano_ratio * 10000000;

	nano::keypair key1, key2, key3;

	// Distribute genesis voting weight
	{
		nano::block_builder builder;
		auto send1 = builder.state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - weight_1)
					 .link (key1.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
					 .build ();
		auto send2 = builder.state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send1->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - weight_1 - weight_2)
					 .link (key2.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send1->hash ()))
					 .build ();
		auto send3 = builder.state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send2->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - weight_1 - weight_2 - weight_3)
					 .link (key3.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send2->hash ()))
					 .build ();
		auto open1 = builder.state ()
					 .account (key1.pub)
					 .previous (0)
					 .representative (key1.pub)
					 .balance (weight_1)
					 .link (send1->hash ())
					 .sign (key1.prv, key1.pub)
					 .work (*system.work.generate (key1.pub))
					 .build ();
		auto open2 = builder.state ()
					 .account (key2.pub)
					 .previous (0)
					 .representative (key2.pub)
					 .balance (weight_2)
					 .link (send2->hash ())
					 .sign (key2.prv, key2.pub)
					 .work (*system.work.generate (key2.pub))
					 .build ();
		auto open3 = builder.state ()
					 .account (key3.pub)
					 .previous (0)
					 .representative (key3.pub)
					 .balance (weight_3)
					 .link (send3->hash ())
					 .sign (key3.prv, key3.pub)
					 .work (*system.work.generate (key3.pub))
					 .build ();
		ASSERT_TRUE (nano::test::process (node_rep1, { send1, send2, send3, open1, open2, open3 }));
		ASSERT_TRUE (nano::test::process (node_rep2, { send1, send2, send3, open1, open2, open3 }));
	}

	// Add rep keys to nodes
	system.wallet (1)->insert_adhoc (key1.prv);
	system.wallet (2)->insert_adhoc (key2.prv);
	system.wallet (2)->insert_adhoc (key3.prv);

	ASSERT_TIMELY_EQ (10s, node.online_reps.online (), weight_1 + weight_2 + weight_3);
	ASSERT_ALWAYS_EQ (1s, node.online_reps.online (), weight_1 + weight_2 + weight_3);
}

// Online weight calculation should include local representative
TEST (online_reps, observe_local)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (10s, node.online_reps.online (), nano::dev::constants.genesis_amount);
	ASSERT_ALWAYS_EQ (1s, node.online_reps.online (), nano::dev::constants.genesis_amount);
}

// Online weight calculation should include slower but active representatives
TEST (online_reps, observe_slow)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());

	// Enough to reach quorum by a single vote
	auto const weight = nano::nano_ratio * 80000000;

	nano::keypair key1, key2; // Fast and slow reps

	// Distribute genesis voting weight
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - weight)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - weight * 2)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (weight)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	auto open2 = builder.state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (weight)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_TRUE (nano::test::process (node, { send1, send2, open1, open2 }));
	nano::test::confirm (node, { send1, send2, open1, open2 });

	ASSERT_TIMELY_EQ (5s, node.active.size (), 0);

	// Add a block that we can vote on
	auto send_dummy = builder.state ()
					  .account (nano::dev::genesis_key.pub)
					  .previous (send2->hash ())
					  .representative (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - weight * 2 - nano::nano_ratio)
					  .link (nano::keypair{}.pub)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*system.work.generate (send2->hash ()))
					  .build ();
	ASSERT_TRUE (nano::test::process (node, { send_dummy }));

	// Wait for election for the block to be activated
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send_dummy->qualified_root ()));
	ASSERT_TRUE (election->contains (send_dummy->hash ()));

	// Issue vote from a fast rep
	auto vote_fast = nano::test::make_final_vote (key1, { send_dummy });
	node.vote_processor.vote_blocking (vote_fast, nano::test::fake_channel (node));

	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_TIMELY (5s, !node.active.active (send_dummy->qualified_root ())); // No longer present in AEC
	ASSERT_TIMELY_EQ (5s, node.online_reps.online (), weight);

	// Issue vote from a slow rep
	auto vote_slow = nano::test::make_final_vote (key2, { send_dummy });
	node.vote_processor.vote_blocking (vote_slow, nano::test::fake_channel (node));

	// The slow rep weight should still be counted as online, even though it arrived slightly after the election already reached quorum
	ASSERT_TIMELY_EQ (5s, node.online_reps.online (), weight * 2);
}

// Test that online weight recalculates when existing representative weights change
TEST (online_reps, weight_change_recalculation)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	ASSERT_EQ (0, node.online_reps.online ());

	nano::keypair key1, key2;
	auto const initial_weight = nano::nano_ratio * 1000;
	auto const additional_weight = nano::nano_ratio * 2000;

	// Create initial distribution
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - initial_weight)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (initial_weight)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	ASSERT_TRUE (nano::test::process (node, { send1, open1 }));

	// Observe the representative - should register initial weight
	node.online_reps.observe (key1.pub);
	ASSERT_EQ (initial_weight, node.online_reps.online ());

	// Create additional weight delegation to the same representative
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - initial_weight - additional_weight)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto open2 = builder.state ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key1.pub) // Delegate to key1
				 .balance (additional_weight)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_TRUE (nano::test::process (node, { send2, open2 }));

	// Observe the same representative again (simulating a vote from existing rep)
	// This should trigger recalculation and pick up the new weight
	node.online_reps.observe (key1.pub);

	// The bug was that online weight would not recalculate for existing representatives
	// With the fix, it should now reflect the updated weight
	ASSERT_TIMELY_EQ (5s, node.online_reps.online (), initial_weight + additional_weight);
}