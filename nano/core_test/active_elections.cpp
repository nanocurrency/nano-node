#include <nano/lib/blocks.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/manual.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <numeric>

using namespace std::chrono_literals;

/*
 * Tests that an election can be confirmed as the result of a confirmation request
 *
 * Set-up:
 * - node1 with:
 * 		- enabled frontiers_confirmation (default) -> allows it to confirm blocks and subsequently generates votes
 * - node2 with:
 * 		- disabled rep crawler -> this inhibits node2 from learning that node1 is a rep
 */
TEST (active_elections, confirm_election_by_request)
{
	nano::test::system system{};
	auto & node1 = *system.add_node ();

	nano::state_block_builder builder{};
	auto send1 = builder
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .link (nano::public_key ())
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	// Process send1 locally on node1
	ASSERT_TRUE (nano::test::process (node1, { send1 }));

	// Add rep key to node1
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// Ensure election on node1 is already confirmed before connecting with node2
	ASSERT_TIMELY (5s, nano::test::confirmed (node1, { send1 }));

	// Wait for the election to be removed and give time for any in-flight vote broadcasts to settle
	ASSERT_TIMELY (5s, node1.active.empty ());
	WAIT (1s);

	// At this point node1 should not generate votes for send1 block unless it receives a request

	// Create a second node
	nano::node_flags node_flags2{};
	node_flags2.disable_rep_crawler = true;
	auto & node2 = *system.add_node (node_flags2);

	// Process send1 block as live block on node2, this should start an election
	node2.process_active (send1);

	// Ensure election is started on node2
	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node2.active.election (send1->qualified_root ())) != nullptr);

	// Ensure election on node2 did not get confirmed without us requesting votes
	WAIT (1s);
	ASSERT_FALSE (election->confirmed ());

	// Expect that node2 has nobody to send a confirmation_request to (no reps)
	ASSERT_EQ (0, election->confirmation_request_count);

	// Get random peer list (of size 1) from node2 -- so basically just node2
	auto const peers = node2.network.random_set (1);
	ASSERT_FALSE (peers.empty ());

	// Add representative (node1) to disabled rep crawler of node2
	node2.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, *peers.cbegin ());

	// Expect a vote to come back
	ASSERT_TIMELY (5s, election->votes ().size () >= 1);

	// There needs to be at least one request to get the election confirmed,
	// Rep has this block already confirmed so should reply with final vote only
	ASSERT_TIMELY (5s, election->confirmation_request_count >= 1);

	// Expect election was confirmed
	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_TIMELY (5s, nano::test::confirmed (node1, { send1 }));
	ASSERT_TIMELY (5s, nano::test::confirmed (node2, { send1 }));
}

TEST (active_elections, confirm_frontier)
{
	nano::test::system system;

	// send 100 raw from genesis to a random account
	nano::state_block_builder builder;
	auto send = builder
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.link (nano::public_key ())
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	{
		// Voting node
		nano::node_flags node_flags;
		node_flags.disable_request_loop = true;
		node_flags.disable_ongoing_bootstrap = true;
		node_flags.disable_ascending_bootstrap = true;
		auto & node1 = *system.add_node (node_flags);
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

		// we cannot use the same block instance on 2 different nodes, so make a copy
		auto send_copy = builder.make_block ().from (*send).build ();
		ASSERT_TRUE (nano::test::process (node1, { send_copy }));
		nano::test::confirm (node1.ledger, send_copy);
	}

	// The rep crawler would otherwise request confirmations in order to find representatives
	nano::node_flags node_flags2;
	node_flags2.disable_ongoing_bootstrap = true;
	node_flags2.disable_ascending_bootstrap = true;
	node_flags2.disable_rep_crawler = true;
	// start node2 later so that we do not get the gossip traffic
	auto & node2 = *system.add_node (node_flags2);

	// Add representative to disabled rep crawler
	auto peers (node2.network.random_set (1));
	ASSERT_FALSE (peers.empty ());
	node2.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, *peers.begin ());

	ASSERT_EQ (nano::block_status::progress, node2.process (send));
	ASSERT_TIMELY (5s, !node2.active.empty ());

	// Save election to check request count afterwards
	std::shared_ptr<nano::election> election2;
	ASSERT_TIMELY (5s, election2 = node2.active.election (send->qualified_root ()));
	ASSERT_TIMELY (5s, nano::test::confirmed (node2, { send }));
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 2);
	ASSERT_TIMELY (5s, node2.active.empty ());
	ASSERT_GT (election2->confirmation_request_count, 0u);
}

TEST (active_elections, keep_local)
{
	nano::test::system system{};

	nano::node_config node_config = system.default_config ();
	node_config.enable_voting = false;
	// Bound to 2, won't drop wallet created transactions, but good to test dropping remote
	node_config.active_elections.size = 2;
	// Disable frontier confirmation to allow the test to finish before
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));

	nano::keypair key1{};
	nano::keypair key2{};
	nano::keypair key3{};
	nano::keypair key4{};
	nano::keypair key5{};
	nano::keypair key6{};

	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	auto const send1 = wallet.send_action (nano::dev::genesis_key.pub, key1.pub, node.config.receive_minimum.number ());
	auto const send2 = wallet.send_action (nano::dev::genesis_key.pub, key2.pub, node.config.receive_minimum.number ());
	auto const send3 = wallet.send_action (nano::dev::genesis_key.pub, key3.pub, node.config.receive_minimum.number ());
	auto const send4 = wallet.send_action (nano::dev::genesis_key.pub, key4.pub, node.config.receive_minimum.number ());
	auto const send5 = wallet.send_action (nano::dev::genesis_key.pub, key5.pub, node.config.receive_minimum.number ());
	auto const send6 = wallet.send_action (nano::dev::genesis_key.pub, key6.pub, node.config.receive_minimum.number ());
	ASSERT_NE (nullptr, send1);
	ASSERT_NE (nullptr, send2);
	ASSERT_NE (nullptr, send3);
	ASSERT_NE (nullptr, send4);
	ASSERT_NE (nullptr, send5);
	ASSERT_NE (nullptr, send6);

	// force-confirm blocks
	nano::test::confirm (node.ledger, send6);

	nano::state_block_builder builder{};
	const auto receive1 = builder.make_block ()
						  .account (key1.pub)
						  .previous (0)
						  .representative (key1.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send1->hash ())
						  .sign (key1.prv, key1.pub)
						  .work (*system.work.generate (key1.pub))
						  .build ();
	const auto receive2 = builder.make_block ()
						  .account (key2.pub)
						  .previous (0)
						  .representative (key2.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send2->hash ())
						  .sign (key2.prv, key2.pub)
						  .work (*system.work.generate (key2.pub))
						  .build ();
	const auto receive3 = builder.make_block ()
						  .account (key3.pub)
						  .previous (0)
						  .representative (key3.pub)
						  .balance (node.config.receive_minimum.number ())
						  .link (send3->hash ())
						  .sign (key3.prv, key3.pub)
						  .work (*system.work.generate (key3.pub))
						  .build ();
	node.process_active (receive1);
	node.process_active (receive2);
	node.process_active (receive3);

	/// bound elections, should drop after one loop
	ASSERT_TIMELY_EQ (5s, node.active.size (), node_config.active_elections.size);
	// ASSERT_EQ (1, node.scheduler.size ());
}

TEST (inactive_votes_cache, basic)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::block_hash latest (node.latest (nano::dev::genesis_key.pub));
	nano::keypair key;
	auto send = nano::send_block_builder ()
				.previous (latest)
				.destination (key.pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send });
	node.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, node.vote_cache.size (), 1);
	node.process_active (send);
	ASSERT_TIMELY (5s, node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), send->hash ()));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

/**
 * This test case confirms that a non final vote cannot cause an election to become confirmed
 */
TEST (inactive_votes_cache, non_final)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	auto send = nano::send_block_builder ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::keypair{}.pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	// Non-final vote
	auto vote = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, 0, 0, std::vector<nano::block_hash> (1, send->hash ()));
	node.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, node.vote_cache.size (), 1);

	node.process_active (send);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send->qualified_root ()));
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached), 1);
	ASSERT_TIMELY_EQ (5s, nano::dev::constants.genesis_amount - 100, election->tally ().begin ()->first);
	ASSERT_FALSE (election->confirmed ());
}

TEST (inactive_votes_cache, fork)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];

	auto const latest = node.latest (nano::dev::genesis_key.pub);
	nano::keypair key{};

	nano::send_block_builder builder{};
	auto send1 = builder.make_block ()
				 .previous (latest)
				 .destination (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (latest)
				 .destination (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();

	auto const vote = nano::test::make_final_vote (nano::dev::genesis_key, { send1 });
	node.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, node.vote_cache.size (), 1);

	node.process_active (send2);

	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node.active.election (send1->qualified_root ())) != nullptr);

	node.process_active (send1);
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);
	ASSERT_TIMELY (5s, node.block_confirmed (send1->hash ()));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (inactive_votes_cache, existing_vote)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::block_hash latest (node.latest (nano::dev::genesis_key.pub));
	nano::keypair key;
	nano::block_builder builder;
	auto send = builder.send ()
				.previous (latest)
				.destination (key.pub)
				.balance (nano::dev::constants.genesis_amount - 100 * nano::Gxrb_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();
	auto open = builder.state ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.balance (100 * nano::Gxrb_ratio)
				.link (send->hash ())
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	node.process_active (send);
	node.block_processor.add (open);
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
	auto election (node.active.election (send->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_GT (node.weight (key.pub), node.minimum_principal_weight ());
	// Insert vote
	auto vote1 = nano::test::make_vote (key, { send }, nano::vote::timestamp_min * 1, 0);
	node.vote_processor.vote (vote1, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, election->votes ().size (), 2);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_new));
	auto last_vote1 (election->votes ()[key.pub]);
	ASSERT_EQ (send->hash (), last_vote1.hash);
	ASSERT_EQ (nano::vote::timestamp_min * 1, last_vote1.timestamp);
	// Attempt to change vote with inactive_votes_cache
	node.vote_cache.insert (vote1);
	auto cached = node.vote_cache.find (send->hash ());
	ASSERT_EQ (1, cached.size ());
	for (auto const & cached_vote : cached)
	{
		node.vote_router.vote (cached_vote);
	}
	// Check that election data is not changed
	ASSERT_EQ (2, election->votes ().size ());
	auto last_vote2 (election->votes ()[key.pub]);
	ASSERT_EQ (last_vote1.hash, last_vote2.hash);
	ASSERT_EQ (last_vote1.timestamp, last_vote2.timestamp);
	ASSERT_EQ (last_vote1.time, last_vote2.time);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (inactive_votes_cache, multiple_votes)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - 100 * nano::Gxrb_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto send2 = builder.send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (100 * nano::Gxrb_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	auto open = builder.state ()
				.account (key1.pub)
				.previous (0)
				.representative (key1.pub)
				.balance (100 * nano::Gxrb_ratio)
				.link (send1->hash ())
				.sign (key1.prv, key1.pub)
				.work (*system.work.generate (key1.pub))
				.build ();

	// put the blocks in the ledger witout triggering an election
	ASSERT_TRUE (nano::test::process (node, { send1, send2, open }));
	ASSERT_TIMELY (5s, nano::test::exists (node, { send1, send2, open }));

	// Process votes
	auto vote1 = nano::test::make_vote (key1, { send1 }, 0, 0);
	node.vote_processor.vote (vote1, std::make_shared<nano::transport::inproc::channel> (node, node));

	auto vote2 = nano::test::make_vote (nano::dev::genesis_key, { send1 }, 0, 0);
	node.vote_processor.vote (vote2, std::make_shared<nano::transport::inproc::channel> (node, node));

	ASSERT_TIMELY_EQ (5s, node.vote_cache.find (send1->hash ()).size (), 2);
	ASSERT_EQ (1, node.vote_cache.size ());
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send1->qualified_root ()));
	ASSERT_TIMELY_EQ (5s, 3, election->votes ().size ()); // 2 votes and 1 default not_an_acount
	ASSERT_EQ (2, node.stats.count (nano::stat::type::election, nano::stat::detail::vote_cached));
}

TEST (inactive_votes_cache, election_start)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	node_config.optimistic_scheduler.enabled = false;
	auto & node = *system.add_node (node_config);
	nano::block_hash latest (node.latest (nano::dev::genesis_key.pub));
	nano::keypair key1, key2;
	nano::send_block_builder send_block_builder;
	nano::state_block_builder state_block_builder;
	// Enough weight to trigger election hinting but not enough to confirm block on its own
	auto amount = ((node.online_reps.trended () / 100) * node.config.hinted_scheduler.hinting_threshold_percent) / 2 + 1000 * nano::Gxrb_ratio;
	auto send1 = send_block_builder.make_block ()
				 .previous (latest)
				 .destination (key1.pub)
				 .balance (nano::dev::constants.genesis_amount - amount)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();
	auto send2 = send_block_builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * amount)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto open1 = state_block_builder.make_block ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (key1.pub)
				 .balance (amount)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*system.work.generate (key1.pub))
				 .build ();
	auto open2 = state_block_builder.make_block ()
				 .account (key2.pub)
				 .previous (0)
				 .representative (key2.pub)
				 .balance (amount)
				 .link (send2->hash ())
				 .sign (key2.prv, key2.pub)
				 .work (*system.work.generate (key2.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	ASSERT_EQ (nano::block_status::progress, node.process (open1));
	ASSERT_EQ (nano::block_status::progress, node.process (open2));
	ASSERT_TIMELY_EQ (5s, 5, node.ledger.block_count ());
	ASSERT_TRUE (node.active.empty ());
	ASSERT_EQ (1, node.ledger.cemented_count ());
	// These blocks will be processed later
	auto send3 = send_block_builder.make_block ()
				 .previous (send2->hash ())
				 .destination (nano::keypair ().pub)
				 .balance (send2->balance_field ().value ().number () - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	auto send4 = send_block_builder.make_block ()
				 .previous (send3->hash ())
				 .destination (nano::keypair ().pub)
				 .balance (send3->balance_field ().value ().number () - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send3->hash ()))
				 .build ();

	// Inactive votes
	auto vote1 = nano::test::make_vote (key1, { open1, open2, send4 });
	node.vote_processor.vote (vote1, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, node.vote_cache.size (), 3);
	ASSERT_TRUE (node.active.empty ());
	ASSERT_EQ (1, node.ledger.cemented_count ());

	// 2 votes are required to start election (dev network)
	auto vote2 = nano::test::make_vote (key2, { open1, open2, send4 });
	node.vote_processor.vote (vote2, std::make_shared<nano::transport::inproc::channel> (node, node));
	// Only election for send1 should start, other blocks are missing dependencies and don't have enough final weight
	ASSERT_TIMELY_EQ (5s, 1, node.active.size ());
	ASSERT_TRUE (node.vote_router.active (send1->hash ()));

	// Confirm elections with weight quorum
	auto vote0 = nano::test::make_final_vote (nano::dev::genesis_key, { open1, open2, send4 });
	node.vote_processor.vote (vote0, std::make_shared<nano::transport::inproc::channel> (node, node));
	ASSERT_TIMELY_EQ (5s, 0, node.active.size ());
	ASSERT_TIMELY_EQ (5s, 5, node.ledger.cemented_count ());
	ASSERT_TRUE (nano::test::confirmed (node, { send1, send2, open1, open2 }));

	// A late block arrival also checks the inactive votes cache
	ASSERT_TRUE (node.active.empty ());
	auto send4_cache (node.vote_cache.find (send4->hash ()));
	ASSERT_EQ (3, send4_cache.size ());
	node.process_active (send3);
	// An election is started for send6 but does not
	ASSERT_FALSE (node.block_confirmed_or_being_confirmed (send3->hash ()));
	// send7 cannot be voted on but an election should be started from inactive votes
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.ledger.tx_begin_read (), *send4));
	node.process_active (send4);
	ASSERT_TIMELY_EQ (5s, 7, node.ledger.cemented_count ());
}

namespace nano
{
TEST (active_elections, vote_replays)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.enable_voting = false;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	nano::keypair key;
	nano::state_block_builder builder;

	// send Gxrb_ratio raw from genesis to key
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_NE (nullptr, send1);

	// create open block for key receing Gxrb_ratio raw
	auto open1 = builder.make_block ()
				 .account (key.pub)
				 .previous (0)
				 .representative (key.pub)
				 .balance (nano::Gxrb_ratio)
				 .link (send1->hash ())
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (key.pub))
				 .build ();
	ASSERT_NE (nullptr, open1);

	// wait for elections objects to appear in the AEC
	node.process_active (send1);
	node.process_active (open1);
	ASSERT_TRUE (nano::test::start_elections (system, node, { send1, open1 }));
	ASSERT_EQ (2, node.active.size ());

	// First vote is not a replay and confirms the election, second vote should be a replay since the election has confirmed but not yet removed
	auto vote_send1 = nano::test::make_final_vote (nano::dev::genesis_key, { send1 });
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote_send1).at (send1->hash ()));
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote_send1).at (send1->hash ()));

	// Wait until the election is removed, at which point the vote is still a replay since it's been recently confirmed
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote_send1).at (send1->hash ()));

	// Open new account
	auto vote_open1 = nano::test::make_final_vote (nano::dev::genesis_key, { open1 });
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote_open1).at (open1->hash ()));
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote_open1).at (open1->hash ()));
	ASSERT_TIMELY (5s, node.active.empty ());
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote_open1).at (open1->hash ()));
	ASSERT_EQ (nano::Gxrb_ratio, node.ledger.weight (key.pub));

	// send 1 raw to key to key
	auto send2 = builder.make_block ()
				 .account (key.pub)
				 .previous (open1->hash ())
				 .representative (key.pub)
				 .balance (nano::Gxrb_ratio - 1)
				 .link (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*system.work.generate (open1->hash ()))
				 .build ();
	ASSERT_NE (nullptr, send2);
	node.process_active (send2);
	ASSERT_TRUE (nano::test::start_elections (system, node, { send2 }));
	ASSERT_EQ (1, node.active.size ());

	// vote2_send2 is a non final vote with little weight, vote1_send2 is the vote that confirms the election
	auto vote1_send2 = nano::test::make_final_vote (nano::dev::genesis_key, { send2 });
	auto vote2_send2 = nano::test::make_vote (key, { send2 }, 0, 0);
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote2_send2).at (send2->hash ())); // this vote cannot confirm the election
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote2_send2).at (send2->hash ())); // this vote cannot confirm the election
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote1_send2).at (send2->hash ())); // this vote confirms the election

	// this should still return replay, either because the election is still in the AEC or because it is recently confirmed
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote1_send2).at (send2->hash ()));
	ASSERT_TIMELY (5s, node.active.empty ());
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote1_send2).at (send2->hash ()));
	ASSERT_EQ (nano::vote_code::replay, node.vote_router.vote (vote2_send2).at (send2->hash ()));

	// Removing blocks as recently confirmed makes every vote indeterminate
	{
		nano::lock_guard<nano::mutex> guard (node.active.mutex);
		node.active.recently_confirmed.clear ();
	}
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_router.vote (vote_send1).at (send1->hash ()));
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_router.vote (vote_open1).at (open1->hash ()));
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_router.vote (vote1_send2).at (send2->hash ()));
	ASSERT_EQ (nano::vote_code::indeterminate, node.vote_router.vote (vote2_send2).at (send2->hash ()));
}
}

// Tests that blocks are correctly cleared from the duplicate filter for unconfirmed elections
TEST (active_elections, dropped_cleanup)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	auto chain = nano::test::setup_chain (system, node, 1, nano::dev::genesis_key, false);
	auto hash = chain[0]->hash ();

	// Add to network filter to ensure proper cleanup after the election is dropped
	std::vector<uint8_t> block_bytes;
	{
		nano::vectorstream stream (block_bytes);
		chain[0]->serialize (stream);
	}
	ASSERT_FALSE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	auto election = nano::test::start_election (system, node, hash);
	ASSERT_NE (nullptr, election);

	// Not yet removed
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));
	ASSERT_TRUE (node.vote_router.active (hash));

	// Now simulate dropping the election
	ASSERT_FALSE (election->confirmed ());
	node.active.erase (*chain[0]);

	// The filter must have been cleared
	ASSERT_FALSE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	// An election was recently dropped
	ASSERT_EQ (1, node.stats.count (nano::stat::type::active_dropped, nano::stat::detail::manual));

	// Block cleared from active
	ASSERT_FALSE (node.vote_router.active (hash));

	// Repeat test for a confirmed election
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	election = nano::test::start_election (system, node, hash);
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, election->confirmed ());
	node.active.erase (*chain[0]);

	// The filter should not have been cleared
	ASSERT_TRUE (node.network.publish_filter.apply (block_bytes.data (), block_bytes.size ()));

	// Not dropped
	ASSERT_EQ (1, node.stats.count (nano::stat::type::active_dropped, nano::stat::detail::manual));

	// Block cleared from active
	ASSERT_FALSE (node.vote_router.active (hash));
}

TEST (active_elections, republish_winner)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	node_config.peering_port = system.get_available_port ();
	auto & node2 = *system.add_node (node_config);

	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	node1.process_active (send1);
	ASSERT_TIMELY (5s, nano::test::exists (node1, { send1 }));
	ASSERT_TIMELY_EQ (3s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in), 1);

	// Several forks
	for (auto i (0); i < 5; i++)
	{
		auto fork = builder.make_block ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - 1 - i)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (nano::dev::genesis->hash ()))
					.build ();
		node1.process_active (fork);
		ASSERT_TIMELY (5s, node1.active.active (*fork));
	}
	ASSERT_TIMELY (3s, !node1.active.empty ());
	ASSERT_EQ (1, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));

	// Process new fork with vote to change winner
	auto fork = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	node1.process_active (fork);
	ASSERT_TIMELY (5s, node1.vote_router.active (fork->hash ()));
	auto election = node1.active.election (fork->qualified_root ());
	ASSERT_NE (nullptr, election);
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { fork });
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node1, node1));
	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_EQ (fork->hash (), election->status.winner->hash ());
	ASSERT_TIMELY (5s, node2.block_confirmed (fork->hash ()));
}

TEST (active_elections, fork_filter_cleanup)
{
	nano::test::system system{};

	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node1 = *system.add_node (node_config);
	nano::keypair key{};
	nano::state_block_builder builder{};
	auto const latest_hash = nano::dev::genesis->hash ();

	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest_hash))
				 .build ();

	std::vector<uint8_t> send_block_bytes{};
	{
		nano::vectorstream stream{ send_block_bytes };
		send1->serialize (stream);
	}

	// Generate 10 forks to prevent new block insertion to election
	for (auto i = 0; i < 10; ++i)
	{
		auto fork = builder.make_block ()
					.previous (latest_hash)
					.account (nano::dev::genesis_key.pub)
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - 1 - i)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest_hash))
					.build ();

		node1.process_active (fork);
		ASSERT_TIMELY (5s, node1.active.election (fork->qualified_root ()) != nullptr);
	}

	// All forks were merged into the same election
	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send1->qualified_root ())) != nullptr);
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 10);
	ASSERT_EQ (1, node1.active.size ());

	// Instantiate a new node
	node_config.peering_port = system.get_available_port ();
	auto & node2 = *system.add_node (node_config);

	// Process the first initial block on node2
	node2.process_active (send1);
	ASSERT_TIMELY (5s, node2.active.election (send1->qualified_root ()) != nullptr);

	// TODO: questions: why doesn't node2 pick up "fork" from node1? because it connected to node1 after node1
	//                  already process_active()d the fork? shouldn't it broadcast it anyway, even later?
	//
	//                  how about node1 picking up "send1" from node2? we know it does because we assert at
	//                  the end that it is within node1's AEC, but why node1.block_count doesn't increase?
	//
	ASSERT_TIMELY_EQ (5s, node2.ledger.block_count (), 2);
	ASSERT_TIMELY_EQ (5s, node1.ledger.block_count (), 2);

	// Block is erased from the duplicate filter
	ASSERT_TIMELY (5s, node1.network.publish_filter.apply (send_block_bytes.data (), send_block_bytes.size ()));
}

/*
 * What this test is doing:
 * Create 20 representatives with minimum principal weight each
 * Create a send block on the genesis account (the last send block)
 * Create 20 forks of the last send block using genesis as representative (no votes produced)
 * Check that only 10 blocks remain in the election (due to max 10 forks per election object limit)
 * Create 20 more forks of the last send block using the new reps as representatives and produce votes for them
 *     (9 votes from this batch should survive and replace existing blocks in the election, why not 10?)
 * Then send winning block and it should replace one of the existing blocks
 */
TEST (active_elections, fork_replacement_tally)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config));

	size_t const reps_count = 20;
	size_t const max_blocks = 10;
	std::vector<nano::keypair> keys (reps_count);
	auto latest (nano::dev::genesis->hash ());
	auto balance (nano::dev::constants.genesis_amount);
	auto amount (node1.minimum_principal_weight ());
	nano::state_block_builder builder;

	// Create 20 representatives & confirm blocks
	for (auto i (0); i < reps_count; i++)
	{
		balance -= amount + i;
		auto send = builder.make_block ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (keys[i].pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		node1.process_active (send);
		latest = send->hash ();
		auto open = builder.make_block ()
					.account (keys[i].pub)
					.previous (0)
					.representative (keys[i].pub)
					.balance (amount + i)
					.link (send->hash ())
					.sign (keys[i].prv, keys[i].pub)
					.work (*system.work.generate (keys[i].pub))
					.build ();
		node1.process_active (open);
		// Confirmation
		auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send, open });
		node1.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node1, node1));
	}
	ASSERT_TIMELY_EQ (5s, node1.ledger.cemented_count (), 1 + 2 * reps_count);

	nano::keypair key;
	auto send_last = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (latest)
					 .representative (nano::dev::genesis_key.pub)
					 .balance (balance - 2 * nano::Gxrb_ratio)
					 .link (key.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (latest))
					 .build ();

	// Forks without votes
	for (auto i (0); i < reps_count; i++)
	{
		auto fork = builder.make_block ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance - nano::Gxrb_ratio - i)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		node1.process_active (fork);
	}

	// Check overflow of blocks
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node1.active.election (send_last->qualified_root ()));
	ASSERT_TIMELY_EQ (5s, max_blocks, election->blocks ().size ());

	// Generate forks with votes to prevent new block insertion to election
	for (auto i (0); i < reps_count; i++)
	{
		auto fork = builder.make_block ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance - 1 - i)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		auto vote = nano::test::make_vote (keys[i], { fork }, 0, 0);
		node1.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node1, node1));
		ASSERT_TIMELY (5s, node1.vote_cache.find (fork->hash ()).size () > 0);
		node1.process_active (fork);
	}

	// function to count the number of rep votes (non genesis) found in election
	// it also checks that there are 10 votes in the election
	auto count_rep_votes_in_election = [&max_blocks, &reps_count, &election, &keys] () {
		// Check that only max weight blocks remains (and start winner)
		auto votes_l = election->votes ();
		if (max_blocks != votes_l.size ())
		{
			return -1;
		}
		int vote_count = 0;
		for (auto i = 0; i < reps_count; i++)
		{
			if (votes_l.find (keys[i].pub) != votes_l.end ())
			{
				vote_count++;
			}
		}
		return vote_count;
	};

	// Check overflow of blocks
	ASSERT_TIMELY_EQ (10s, count_rep_votes_in_election (), 9);
	ASSERT_EQ (max_blocks, election->blocks ().size ());

	// Process correct block
	node_config.peering_port = system.get_available_port ();
	auto & node2 (*system.add_node (node_config));
	node1.network.publish_filter.clear ();
	node2.network.flood_block (send_last);
	ASSERT_TIMELY (3s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) > 0);

	// Correct block without votes is ignored
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks1;
	ASSERT_TIMELY_EQ (5s, max_blocks, (blocks1 = election->blocks (), blocks1.size ()));
	ASSERT_FALSE (blocks1.find (send_last->hash ()) != blocks1.end ());

	// Process vote for correct block & replace existing lowest tally block
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { send_last }, 0, 0);
	node1.vote_processor.vote (vote, std::make_shared<nano::transport::inproc::channel> (node1, node1));
	// ensure vote arrives before the block
	ASSERT_TIMELY_EQ (5s, 1, node1.vote_cache.find (send_last->hash ()).size ());
	node1.network.publish_filter.clear ();
	node2.network.flood_block (send_last);
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) > 1);

	// the send_last block should replace one of the existing block of the election because it has higher vote weight
	auto find_send_last_block = [&election, &send_last] () {
		auto blocks2 = election->blocks ();
		return blocks2.find (send_last->hash ()) != blocks2.end ();
	};
	ASSERT_TIMELY (5s, find_send_last_block ())
	ASSERT_EQ (max_blocks, election->blocks ().size ());

	ASSERT_TIMELY_EQ (5s, count_rep_votes_in_election (), 8);

	auto votes2 (election->votes ());
	ASSERT_TRUE (votes2.find (nano::dev::genesis_key.pub) != votes2.end ());
}

namespace nano
{
// Blocks that won an election must always be seen as confirming or cemented
TEST (active_elections, confirmation_consistency)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	for (unsigned i = 0; i < 10; ++i)
	{
		auto block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, nano::public_key (), node.config.receive_minimum.number ()));
		ASSERT_NE (nullptr, block);
		system.deadline_set (5s);
		while (!node.ledger.confirmed.block_exists_or_pruned (node.ledger.tx_begin_read (), block->hash ()))
		{
			node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
			ASSERT_NO_ERROR (system.poll (5ms));
		}
		ASSERT_NO_ERROR (system.poll_until_true (1s, [&node, &block, i] {
			nano::lock_guard<nano::mutex> guard (node.active.mutex);
			EXPECT_EQ (i + 1, node.active.recently_confirmed.size ());
			EXPECT_EQ (block->qualified_root (), node.active.recently_confirmed.back ().first);
			return i + 1 == node.active.recently_cemented.size (); // done after a callback
		}));
	}
}
}

TEST (active_elections, confirm_new)
{
	nano::test::system system (1);
	auto & node1 = *system.nodes[0];
	auto send = nano::send_block_builder ()
				.previous (nano::dev::genesis->hash ())
				.destination (nano::public_key ())
				.balance (nano::dev::constants.genesis_amount - 100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	node1.process_active (send);
	ASSERT_TIMELY_EQ (5s, 1, node1.active.size ());
	auto & node2 = *system.add_node ();
	// Add key to node2
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	// Let node2 know about the block
	auto send_copy = nano::send_block_builder ().make_block ().from (*send).build ();
	ASSERT_TIMELY (5s, node2.block (send_copy->hash ()));
	// Wait confirmation
	ASSERT_TIMELY (5s, node1.ledger.cemented_count () == 2);
	ASSERT_TIMELY (5s, node2.ledger.cemented_count () == 2);
}

// Ensures votes are tallied on election::publish even if no vote is inserted through inactive_votes_cache
TEST (active_elections, conflicting_block_vote_existing_election)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node = *system.add_node (node_flags);
	nano::keypair key;
	nano::state_block_builder builder;
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 100)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	auto fork = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 200)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	auto vote_fork = nano::test::make_final_vote (nano::dev::genesis_key, { fork });

	ASSERT_EQ (nano::block_status::progress, node.process_local (send).value ());
	ASSERT_TIMELY_EQ (5s, 1, node.active.size ());

	// Vote for conflicting block, but the block does not yet exist in the ledger
	node.vote_router.vote (vote_fork);

	// Block now gets processed
	ASSERT_EQ (nano::block_status::fork, node.process_local (fork).value ());

	// Election must be confirmed
	auto election (node.active.election (fork->qualified_root ()));
	ASSERT_NE (nullptr, election);
	ASSERT_TIMELY (3s, election->confirmed ());
}

TEST (active_elections, activate_account_chain)
{
	nano::test::system system;
	nano::node_flags flags;
	nano::node_config config = system.default_config ();
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	nano::keypair key;
	nano::state_block_builder builder;
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.link (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 3)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send2->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();
	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (open->hash ())
				   .representative (key.pub)
				   .link (send3->hash ())
				   .balance (2)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (open->hash ()))
				   .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	ASSERT_EQ (nano::block_status::progress, node.process (send3));
	ASSERT_EQ (nano::block_status::progress, node.process (open));
	ASSERT_EQ (nano::block_status::progress, node.process (receive));

	node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, node.active.election (send->qualified_root ()));
	auto election1 = node.active.election (send->qualified_root ());
	ASSERT_EQ (1, node.active.size ());
	ASSERT_EQ (1, election1->blocks ().count (send->hash ()));
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	auto election2 = node.active.election (send->qualified_root ());
	ASSERT_EQ (election2, election1);
	election1->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send->hash ()));
	// On cementing, the next election is started
	ASSERT_TIMELY (3s, node.active.active (send2->qualified_root ()));
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	auto election3 = node.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election3);
	ASSERT_EQ (1, election3->blocks ().count (send2->hash ()));
	election3->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send2->hash ()));
	// On cementing, the next election is started
	ASSERT_TIMELY (3s, node.active.active (open->qualified_root ()));
	ASSERT_TIMELY (3s, node.active.active (send3->qualified_root ()));
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	auto election4 = node.active.election (send3->qualified_root ());
	ASSERT_NE (nullptr, election4);
	ASSERT_EQ (1, election4->blocks ().count (send3->hash ()));
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), key.pub);
	auto election5 = node.active.election (open->qualified_root ());
	ASSERT_NE (nullptr, election5);
	ASSERT_EQ (1, election5->blocks ().count (open->hash ()));
	election5->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (open->hash ()));
	// Until send3 is also confirmed, the receive block should not activate
	std::this_thread::sleep_for (200ms);
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), key.pub);
	election4->force_confirm ();
	ASSERT_TIMELY (3s, node.block_confirmed (send3->hash ()));
	ASSERT_TIMELY (3s, node.active.active (receive->qualified_root ()));
}

TEST (active_elections, activate_inactive)
{
	nano::test::system system;
	nano::node_flags flags;
	nano::node_config config = system.default_config ();
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node = *system.add_node (config, flags);

	nano::keypair key;
	nano::state_block_builder builder;
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.link (key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (nano::keypair ().pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	ASSERT_EQ (nano::block_status::progress, node.process (send));
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	auto election = nano::test::start_election (system, node, send2->hash ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY (5s, !node.confirming_set.exists (send2->hash ()));
	ASSERT_TIMELY (5s, node.block_confirmed (send2->hash ()));
	ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()));

	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
	ASSERT_ALWAYS_EQ (50ms, 0, node.stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));

	// The first block was not active so no activation takes place
	ASSERT_FALSE (node.active.active (open->qualified_root ()) || node.block_confirmed_or_being_confirmed (open->hash ()));
}

TEST (active_elections, list_active)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	nano::keypair key;
	nano::state_block_builder builder;
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.link (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	ASSERT_EQ (nano::block_status::progress, node.process (send));

	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, node.process (send2));

	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send2->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	ASSERT_EQ (nano::block_status::progress, node.process (open));

	ASSERT_TRUE (nano::test::start_elections (system, node, { send, send2, open }));
	ASSERT_EQ (3, node.active.size ());
	ASSERT_EQ (1, node.active.list_active (1).size ());
	ASSERT_EQ (2, node.active.list_active (2).size ());
	ASSERT_EQ (3, node.active.list_active (3).size ());
	ASSERT_EQ (3, node.active.list_active (4).size ());
	ASSERT_EQ (3, node.active.list_active (99999).size ());
	ASSERT_EQ (3, node.active.list_active ().size ());

	auto active = node.active.list_active ();
}

/*
 * Ensures we limit the number of vote hinted elections in AEC
 */
TEST (active_elections, limit_vote_hinted_elections)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	const int aec_limit = 10;
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	config.optimistic_scheduler.enabled = false;
	config.active_elections.size = aec_limit;
	config.active_elections.hinted_limit_percentage = 10; // Should give us a limit of 1 hinted election
	auto & node = *system.add_node (config);

	// Setup representatives
	// Enough weight to trigger election hinting but not enough to confirm block on its own
	const auto amount = ((node.online_reps.trended () / 100) * node.config.hinted_scheduler.hinting_threshold_percent) + 1000 * nano::Gxrb_ratio;
	nano::keypair rep1 = nano::test::setup_rep (system, node, amount / 2);
	nano::keypair rep2 = nano::test::setup_rep (system, node, amount / 2);

	auto blocks = nano::test::setup_independent_blocks (system, node, 2);
	auto open0 = blocks[0];
	auto open1 = blocks[1];

	// Even though automatic frontier confirmation is disabled, AEC is doing funny stuff and inserting elections, clear that
	WAIT (1s);
	node.active.clear ();
	ASSERT_TRUE (node.active.empty ());

	// Inactive vote
	auto vote1 = nano::test::make_vote (rep1, { open0, open1 });
	node.vote_processor.vote (vote1, nano::test::fake_channel (node));
	// Ensure new inactive vote cache entries were created
	ASSERT_TIMELY_EQ (5s, node.vote_cache.size (), 2);
	// And no elections are getting started yet
	ASSERT_ALWAYS (1s, node.active.empty ());
	// And nothing got confirmed yet
	ASSERT_FALSE (nano::test::confirmed (node, { open0, open1 }));

	// This vote should trigger election hinting for first receive block
	auto vote2 = nano::test::make_vote (rep2, { open0 });
	node.vote_processor.vote (vote2, nano::test::fake_channel (node));
	// Ensure an election got started for open0 block
	ASSERT_TIMELY_EQ (5s, node.active.size (), 1);
	ASSERT_TIMELY (5s, nano::test::active (node, { open0 }));

	// This vote should trigger election hinting but not become active due to limit of active hinted elections
	auto vote3 = nano::test::make_vote (rep2, { open1 });
	node.vote_processor.vote (vote3, nano::test::fake_channel (node));
	// Ensure no new election are getting started
	ASSERT_NEVER (1s, nano::test::active (node, { open1 }));
	ASSERT_EQ (node.active.size (), 1);

	// This final vote should confirm the first receive block
	auto vote4 = nano::test::make_final_vote (nano::dev::genesis_key, { open0 });
	node.vote_processor.vote (vote4, nano::test::fake_channel (node));
	// Ensure election for open0 block got confirmed
	ASSERT_TIMELY (5s, nano::test::confirmed (node, { open0 }));

	// Now a second block should get vote hinted
	ASSERT_TIMELY (5s, nano::test::active (node, { open1 }));

	// Ensure there was no overflow of elections
	ASSERT_EQ (0, node.stats.count (nano::stat::type::active_dropped, nano::stat::detail::priority));
}

