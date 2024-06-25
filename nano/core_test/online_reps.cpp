#include <nano/node/online_reps.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (online_reps, basic)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	auto vote (std::make_shared<nano::vote> ());
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.online_reps.observe (nano::dev::genesis_key.pub);
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.online ());
	// 1 minimum, 1 maximum
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	node1.online_reps.sample ();
	ASSERT_EQ (nano::dev::constants.genesis_amount, node1.online_reps.trended ());
	node1.online_reps.clear ();
	// 2 minimum, 1 maximum
	node1.online_reps.sample ();
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
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
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
	ASSERT_EQ (nano::dev::constants.genesis_amount - nano::Gxrb_ratio, node1.online_reps.online ());
}