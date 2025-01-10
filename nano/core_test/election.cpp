#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/online_reps.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (election, construction)
{
	celerix::test::system system (1);
	auto & node = *system.nodes[0];
	auto election = std::make_shared<celerix::election> (
	node, celerix::dev::genesis, [] (auto const &) {}, [] (auto const &) {}, celerix::election_behavior::priority);
}

TEST (election, behavior)
{
	celerix::test::system system (1);
	auto chain = celerix::test::setup_chain (system, *system.nodes[0], 1, celerix::dev::genesis_key, false);
	auto election = celerix::test::start_election (system, *system.nodes[0], chain[0]->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (celerix::election_behavior::manual, election->behavior ());
}

TEST (election, quorum_minimum_flip_success)
{
	celerix::test::system system{};

	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = celerix::dev::constants.genesis_amount;
	node_config.backlog_scan.enable = false;

	auto & node1 = *system.add_node (node_config);
	auto const latest_hash = celerix::dev::genesis->hash ();
	celerix::state_block_builder builder{};

	celerix::keypair key1{};
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (celerix::dev::genesis_key.pub)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key1.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();

	celerix::keypair key2{};
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .account (celerix::dev::genesis_key.pub)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key2.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()) != nullptr)

	node1.process_active (send2);
	std::shared_ptr<celerix::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send2->qualified_root ())) != nullptr)
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { send2->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote).at (send2->hash ()));

	ASSERT_TIMELY (5s, election->confirmed ());
	auto const winner = election->winner ();
	ASSERT_NE (nullptr, winner);
	ASSERT_EQ (*winner, *send2);
}

TEST (election, quorum_minimum_flip_fail)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = celerix::dev::constants.genesis_amount;
	node_config.backlog_scan.enable = false;
	auto & node = *system.add_node (node_config);
	celerix::state_block_builder builder;

	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .account (celerix::dev::genesis_key.pub)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node.online_reps.delta () - 1)
				 .link (celerix::keypair{}.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .account (celerix::dev::genesis_key.pub)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node.online_reps.delta () - 1)
				 .link (celerix::keypair{}.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();

	// process send1 and wait until its election appears
	node.process_active (send1);
	ASSERT_TIMELY (5s, node.active.election (send1->qualified_root ()))

	// process send2 and wait until it is added to the existing election
	node.process_active (send2);
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()))
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	// genesis generates a final vote for send2 but it should not be enough to reach quorum due to the online_weight_minimum being so high
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { send2->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node.vote_router.vote (vote).at (send2->hash ()));

	// give the election some time before asserting it is not confirmed so that in case
	// it would be wrongfully confirmed, have that immediately fail instead of race
	WAIT (1s);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_FALSE (node.block_confirmed (send2->hash ()));
}

// This test ensures blocks can be confirmed precisely at the quorum minimum
TEST (election, quorum_minimum_confirm_success)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = celerix::dev::constants.genesis_amount;
	node_config.backlog_scan.enable = false;
	auto & node1 = *system.add_node (node_config);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ()) // Only minimum quorum remains
				 .link (key1.pub)
				 .work (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	auto election = celerix::test::start_election (system, node1, send1->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote).at (send1->hash ()));
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_TIMELY (5s, election->confirmed ());
}

// checks that block cannot be confirmed if there is no enough votes to reach quorum
TEST (election, quorum_minimum_confirm_fail)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = celerix::dev::constants.genesis_amount;
	node_config.backlog_scan.enable = false;
	auto & node1 = *system.add_node (node_config);

	celerix::block_builder builder;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta () - 1)
				 .link (celerix::keypair{}.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .build ();

	node1.process_active (send1);
	auto election = celerix::test::start_election (system, node1, send1->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());

	auto vote = celerix::test::make_final_vote (celerix::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote).at (send1->hash ()));

	// give the election a chance to confirm
	WAIT (1s);

	// it should not confirm because there should not be enough quorum
	ASSERT_TRUE (node1.block (send1->hash ()));
	ASSERT_FALSE (election->confirmed ());
}

namespace celerix
{
// FIXME: this test fails on rare occasions. It needs a review.
TEST (election, quorum_minimum_update_weight_before_quorum_checks)
{
	celerix::test::system system;

	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;

	auto & node1 = *system.add_node (node_config);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	celerix::keypair key1;
	celerix::send_block_builder builder;
	auto const amount = ((celerix::uint256_t (node_config.online_weight_minimum.number ()) * celerix::online_reps::online_weight_quorum) / 100).convert_to<celerix::uint128_t> () - 1;

	auto const latest = node1.latest (celerix::dev::genesis_key.pub);
	auto const send1 = builder.make_block ()
					   .previous (latest)
					   .destination (key1.pub)
					   .balance (amount)
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*system.work.generate (latest))
					   .build ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);

	auto const open1 = celerix::open_block_builder{}.make_block ().account (key1.pub).source (send1->hash ()).representative (key1.pub).sign (key1.prv, key1.pub).work (*system.work.generate (key1.pub)).build ();
	ASSERT_EQ (celerix::block_status::progress, node1.process (open1));

	celerix::keypair key2;
	auto const send2 = builder.make_block ()
					   .previous (open1->hash ())
					   .destination (key2.pub)
					   .balance (3)
					   .sign (key1.prv, key1.pub)
					   .work (*system.work.generate (open1->hash ()))
					   .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.process (send2));
	ASSERT_TIMELY_EQ (5s, node1.ledger.block_count (), 4);

	node_config.peering_port = system.get_available_port ();
	auto & node2 = *system.add_node (node_config);

	system.wallet (1)->insert_adhoc (key1.prv);
	ASSERT_TIMELY_EQ (10s, node2.ledger.block_count (), 4);

	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, (election = node1.active.election (send1->qualified_root ())) != nullptr);
	ASSERT_EQ (1, election->blocks ().size ());

	auto vote1 = celerix::test::make_final_vote (celerix::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote1).at (send1->hash ()));

	auto channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (channel, nullptr);

	auto vote2 = celerix::test::make_final_vote (key1, { send1->hash () });
	node1.rep_crawler.force_process (vote2, channel);

	ASSERT_FALSE (election->confirmed ());

	// Modify online_m for online_reps to more than is available, this checks that voting below updates it to current online reps.
	node1.online_reps.force_online_weight (node_config.online_weight_minimum.number () + 20);
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote2).at (send1->hash ()));
	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
}
}

TEST (election, continuous_voting)
{
	celerix::test::system system{};
	auto & node1 = *system.add_node ();
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);

	// We want genesis to have just enough voting weight to be a principal rep, but not enough to confirm blocks on their own
	celerix::keypair key1{};
	celerix::send_block_builder builder{};
	auto send1 = builder.make_block ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (node1.balance (celerix::dev::genesis_key.pub) / 10 * 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();

	ASSERT_TRUE (celerix::test::process (node1, { send1 }));
	celerix::test::confirm (node1.ledger, send1);

	node1.stats.clear ();

	// Create a block that should be staying in AEC but not get confirmed
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (node1.balance (celerix::dev::genesis_key.pub) - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	ASSERT_TRUE (celerix::test::process (node1, { send2 }));
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// Ensure votes are broadcasted in continuous manner
	ASSERT_TIMELY (5s, node1.stats.count (celerix::stat::type::election, celerix::stat::detail::broadcast_vote) >= 5);
}
