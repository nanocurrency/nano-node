#include <celerix/node/online_reps.hpp>
#include <celerix/node/transport/fake.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (online_reps, basic)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	// 1 sample of minimum weight
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	auto vote (std::make_shared<celerix::vote> ());
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.online_reps.observe (celerix::dev::genesis_key.pub);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node1.online_reps.online ());
	// 1 minimum, 1 maximum
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
	node1.online_reps.force_sample ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node1.online_reps.trended ());
	node1.online_reps.clear ();
	// 2 minimum, 1 maximum
	node1.online_reps.force_sample ();
	ASSERT_EQ (node1.config.online_weight_minimum, node1.online_reps.trended ());
}

TEST (online_reps, rep_crawler)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::milliseconds_since_epoch (), 0, std::vector<celerix::block_hash>{ celerix::dev::genesis->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	// Without rep crawler
	node1.vote_processor.vote_blocking (vote, std::make_shared<celerix::transport::fake::channel> (node1));
	ASSERT_EQ (0, node1.online_reps.online ());
	// After inserting to rep crawler
	auto channel = std::make_shared<celerix::transport::fake::channel> (node1);
	node1.rep_crawler.force_query (celerix::dev::genesis->hash (), channel);
	node1.vote_processor.vote_blocking (vote, channel);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node1.online_reps.online ());
}

TEST (online_reps, election)
{
	celerix::test::system system;
	celerix::node_flags flags;
	flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (flags);
	// Start election
	celerix::keypair key;
	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*node1.work_generate_blocking (celerix::dev::genesis->hash ()))
				 .build ();
	node1.process_active (send1);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	// Process vote for ongoing election
	auto vote = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::milliseconds_since_epoch (), 0, std::vector<celerix::block_hash>{ send1->hash () });
	ASSERT_EQ (0, node1.online_reps.online ());
	node1.vote_processor.vote_blocking (vote, std::make_shared<celerix::transport::fake::channel> (node1));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, node1.online_reps.online ());
}