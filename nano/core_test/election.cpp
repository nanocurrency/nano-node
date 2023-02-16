#include <nano/node/election.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (election, construction)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	node.block_confirm (nano::dev::genesis);
	ASSERT_TIMELY (5s, node.active.election (nano::dev::genesis->qualified_root ()));
	auto election = node.active.election (nano::dev::genesis->qualified_root ());
	election->transition_active ();
}

TEST (election, behavior)
{
	nano::test::system system (1);
	auto election = nano::test::start_election (system, *system.nodes[0], nano::dev::genesis->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (nano::election_behavior::normal, election->behavior ());
}

TEST (election, quorum_minimum_flip_success)
{
	nano::test::system system{};

	nano::node_config node_config{ nano::test::get_available_port (), system.logging };
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node1 = *system.add_node (node_config);
	auto const latest_hash = nano::dev::genesis->hash ();
	nano::state_block_builder builder{};

	nano::keypair key1{};
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key1.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();

	nano::keypair key2{};
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key2.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()) != nullptr)

	node1.process_active (send2);
	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send2->qualified_root ())) != nullptr)
	ASSERT_TIMELY (5s, election->blocks ().size () == 2);

	auto const vote1 = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send2->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));

	ASSERT_TIMELY (5s, election->confirmed ());
	auto const winner = election->winner ();
	ASSERT_NE (nullptr, winner);
	ASSERT_EQ (*winner, *send2);
}

TEST (election, quorum_minimum_flip_fail)
{
	nano::test::system system{};

	nano::node_config node_config{ nano::test::get_available_port (), system.logging };
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node1 = *system.add_node (node_config);
	auto const latest_hash = nano::dev::genesis->hash ();
	nano::state_block_builder builder{};

	nano::keypair key1{};
	auto send1 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta () - 1)
				 .link (key1.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();

	nano::keypair key2{};
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta () - 1)
				 .link (key2.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()) != nullptr)

	node1.process_active (send2);
	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send2->qualified_root ())) != nullptr)
	ASSERT_TIMELY (5s, election->blocks ().size () == 2);

	auto const vote1 = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send2->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));

	// give the election 5 seconds before asserting it is not confirmed so that in case
	// it would be wrongfully confirmed, have that immediately fail instead of race
	//
	WAIT (1s);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_FALSE (node1.block_confirmed (send2->hash ()));
}

TEST (election, quorum_minimum_confirm_success)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key1.pub)
				 .work (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	node1.block_processor.flush ();
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send1->hash () }));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_TRUE (election->confirmed ());
}

TEST (election, quorum_minimum_confirm_fail)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta () - 1)
				 .link (key1.pub)
				 .work (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build_shared ();
	node1.work_generate_blocking (*send1);
	node1.process_active (send1);
	node1.block_processor.flush ();
	node1.scheduler.activate (nano::dev::genesis_key.pub, node1.store.tx_begin_read ());
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()));
	auto election = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());
	auto vote1 (std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send1->hash () }));
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));
	node1.block_processor.flush ();
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_FALSE (election->confirmed ());
}

namespace nano
{
// FIXME: this test fails on rare occasions. It needs a review.
TEST (election, quorum_minimum_update_weight_before_quorum_checks)
{
	nano::test::system system{};

	nano::node_config node_config{ nano::test::get_available_port (), system.logging };
	node_config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node1 = *system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	nano::keypair key1{};
	nano::send_block_builder builder{};
	auto const amount = ((nano::uint256_t (node_config.online_weight_minimum.number ()) * nano::online_reps::online_weight_quorum) / 100).convert_to<nano::uint128_t> () - 1;

	auto const latest = node1.latest (nano::dev::genesis_key.pub);
	auto const send1 = builder.make_block ()
					   .previous (latest)
					   .destination (key1.pub)
					   .balance (amount)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (*system.work.generate (latest))
					   .build_shared ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);

	auto const open1 = nano::open_block_builder{}.make_block ().account (key1.pub).source (send1->hash ()).representative (key1.pub).sign (key1.prv, key1.pub).work (*system.work.generate (key1.pub)).build_shared ();
	ASSERT_EQ (nano::process_result::progress, node1.process (*open1).code);

	nano::keypair key2{};
	auto const send2 = builder.make_block ()
					   .previous (open1->hash ())
					   .destination (key2.pub)
					   .balance (3)
					   .sign (key1.prv, key1.pub)
					   .work (*system.work.generate (open1->hash ()))
					   .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node1.process (*send2).code);
	ASSERT_TIMELY (5s, node1.ledger.cache.block_count == 4);

	node_config.peering_port = nano::test::get_available_port ();
	auto & node2 = *system.add_node (node_config);

	system.wallet (1)->insert_adhoc (key1.prv);
	ASSERT_TIMELY (10s, node2.ledger.cache.block_count == 4);

	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send1->qualified_root ())) != nullptr);
	ASSERT_EQ (1, election->blocks ().size ());

	auto const vote1 = std::make_shared<nano::vote> (nano::dev::genesis_key.pub, nano::dev::genesis_key.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send1->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote1));

	auto channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (channel, nullptr);

	auto const vote2 = std::make_shared<nano::vote> (key1.pub, key1.prv, nano::vote::timestamp_max, nano::vote::duration_max, std::vector<nano::block_hash>{ send1->hash () });
	ASSERT_FALSE (node1.rep_crawler.response (channel, vote2, true));

	ASSERT_FALSE (election->confirmed ());
	{
		nano::lock_guard<nano::mutex> guard (node1.online_reps.mutex);
		// Modify online_m for online_reps to more than is available, this checks that voting below updates it to current online reps.
		node1.online_reps.online_m = node_config.online_weight_minimum.number () + 20;
	}
	ASSERT_EQ (nano::vote_code::vote, node1.active.vote (vote2));
	ASSERT_TRUE (election->confirmed ());
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
}
}

TEST (election, continuous_voting)
{
	nano::test::system system{};
	auto & node1 = *system.add_node ();
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	// We want genesis to have just enough voting weight to be a principal rep, but not enough to confirm blocks on their own
	nano::keypair key1{};
	nano::send_block_builder builder{};
	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (node1.balance (nano::dev::genesis_key.pub) / 10 * 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();

	ASSERT_TRUE (nano::test::process (node1, { send1 }));
	ASSERT_TIMELY (5s, nano::test::confirm (node1, { send1 }));
	ASSERT_TIMELY (5s, nano::test::confirmed (node1, { send1 }));

	node1.stats.clear ();

	// Create a block that should be staying in AEC but not get confirmed
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (node1.balance (nano::dev::genesis_key.pub) - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();

	ASSERT_TRUE (nano::test::process (node1, { send2 }));
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// Ensure votes are generated in continuous manner
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::election, nano::stat::detail::generate_vote) >= 5);
}
