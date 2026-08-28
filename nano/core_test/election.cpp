#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/online_reps.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>

using namespace std::chrono_literals;

TEST (election, construction)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto election = std::make_shared<nano::election> (
	node, nano::dev::genesis, nano::election_behavior::priority, 0, [] (auto const &) {}, [] (auto const &) {}, [] (auto const &) {});
}

// Ticking an active election broadcasts a vote for the current winner via the vote generator
TEST (election, tick_broadcast_vote)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv); // Local representative that can vote
	auto election = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 42);
	ASSERT_TRUE (election->transition_active ());
	auto const now = std::chrono::steady_clock::now ();
	election->tick (now);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_vote_normal));
	// Vote broadcasts are paced, an immediate second tick does not vote again
	election->tick (now);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_vote_normal));
}

// A confirmed election broadcasts a final vote
TEST (election, broadcast_vote_final)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv); // Local representative that can vote
	auto election = std::make_shared<nano::election> (node, nano::dev::genesis, nano::election_behavior::priority, 42);
	ASSERT_TRUE (election->transition_active ());
	election->force_confirm ();
	election->broadcast_vote ();
	ASSERT_EQ (1, node.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_vote_final));
}

TEST (election, behavior)
{
	nano::test::system system (1);
	auto chain = nano::test::setup_chain (system, *system.nodes[0], 1, nano::dev::genesis_key, false);
	auto election = nano::test::start_election (system, *system.nodes[0], chain[0]->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (nano::election_behavior::manual, election->behavior ());
	ASSERT_EQ (nano::election_behavior::manual, election->get_extended_status ().behavior);
}

TEST (election, quorum_minimum_flip_success)
{
	nano::test::system system{};

	nano::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.backlog_scan->enable = false;

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
				 .build ();

	nano::keypair key2{};
	auto send2 = builder.make_block ()
				 .previous (latest_hash)
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ())
				 .link (key2.pub)
				 .work (*system.work.generate (latest_hash))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();

	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()) != nullptr)

	node1.process_active (send2);
	std::shared_ptr<nano::election> election{};
	ASSERT_TIMELY (5s, (election = node1.active.election (send2->qualified_root ())) != nullptr)
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send2->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.vote_router.vote (vote).at (send2->hash ()));

	ASSERT_TIMELY (5s, election->confirmed ());
	auto const winner = election->winner ();
	ASSERT_NE (nullptr, winner);
	ASSERT_EQ (*winner, *send2);
}

TEST (election, quorum_minimum_flip_fail)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::state_block_builder builder;

	auto send1 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node.online_reps.delta () - 1)
				 .link (nano::keypair{}.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();

	auto send2 = builder.make_block ()
				 .previous (nano::dev::genesis->hash ())
				 .account (nano::dev::genesis_key.pub)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node.online_reps.delta () - 1)
				 .link (nano::keypair{}.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();

	// process send1 and wait until its election appears
	node.process_active (send1);
	ASSERT_TIMELY (5s, node.active.election (send1->qualified_root ()))

	// process send2 and wait until it is added to the existing election
	node.process_active (send2);
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send2->qualified_root ()))
	ASSERT_TIMELY_EQ (5s, election->blocks ().size (), 2);

	// genesis generates a final vote for send2 but it should not be enough to reach quorum due to the online_weight_minimum being so high
	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send2->hash () });
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote).at (send2->hash ()));

	// give the election some time before asserting it is not confirmed so that in case
	// it would be wrongfully confirmed, have that immediately fail instead of race
	WAIT (1s);
	ASSERT_FALSE (election->confirmed ());
	ASSERT_FALSE (node.block_confirmed (send2->hash ()));
}

// This test ensures blocks can be confirmed precisely at the quorum minimum
TEST (election, quorum_minimum_confirm_success)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.backlog_scan->enable = false;
	auto & node1 = *system.add_node (node_config);
	nano::keypair key1;
	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta ()) // Only minimum quorum remains
				 .link (key1.pub)
				 .work (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();
	node1.work_generate_blocking (*send1);

	nano::test::process (node1, { send1 });
	auto election = nano::test::start_election (system, node1, send1->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.vote_router.vote (vote).at (send1->hash ()));
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
	ASSERT_TIMELY (5s, election->confirmed ());
}

// checks that block cannot be confirmed if there is no enough votes to reach quorum
TEST (election, quorum_minimum_confirm_fail)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = nano::dev::constants.genesis_amount;
	node_config.backlog_scan->enable = false;
	auto & node1 = *system.add_node (node_config);

	nano::block_builder builder;
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (node1.online_reps.delta () - 1)
				 .link (nano::keypair{}.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .build ();

	nano::test::process (node1, { send1 });
	auto election = nano::test::start_election (system, node1, send1->hash ());
	ASSERT_NE (nullptr, election);
	ASSERT_EQ (1, election->blocks ().size ());

	auto vote = nano::test::make_final_vote (nano::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.vote_router.vote (vote).at (send1->hash ()));

	// give the election a chance to confirm
	WAIT (1s);

	// it should not confirm because there should not be enough quorum
	ASSERT_TRUE (node1.block (send1->hash ()));
	ASSERT_FALSE (election->confirmed ());
}

// FIXME: this test fails on rare occasions. It needs a review.
TEST (election, quorum_minimum_update_weight_before_quorum_checks)
{
	nano::test::system system;

	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;

	auto & node1 = *system.add_node (node_config);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);

	nano::keypair key1;
	nano::send_block_builder builder;
	auto const amount = ((nano::uint256_t (node_config.online_weight_minimum.number ()) * nano::online_reps::online_weight_quorum) / 100).convert_to<nano::uint128_t> () - 1;

	auto const latest = node1.latest (nano::dev::genesis_key.pub);
	auto const send1 = builder.make_block ()
					   .previous (latest)
					   .destination (key1.pub)
					   .balance (amount)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (*system.work.generate (latest))
					   .build ();
	node1.process_active (send1);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);

	auto const open1 = nano::open_block_builder{}.make_block ().account (key1.pub).source (send1->hash ()).representative (key1.pub).sign (key1.prv, key1.pub).work (*system.work.generate (key1.pub)).build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (open1));

	nano::keypair key2;
	auto const send2 = builder.make_block ()
					   .previous (open1->hash ())
					   .destination (key2.pub)
					   .balance (3)
					   .sign (key1.prv, key1.pub)
					   .work (*system.work.generate (open1->hash ()))
					   .build ();
	ASSERT_EQ (nano::block_status::progress, node1.process (send2));
	ASSERT_TIMELY_EQ (5s, node1.ledger.block_count (), 4);

	node_config.peering_port = system.get_available_port ();
	auto & node2 = *system.add_node (node_config);

	system.wallet (1)->insert_adhoc (key1.prv);
	ASSERT_TIMELY_EQ (10s, node2.ledger.block_count (), 4);

	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, (election = node1.active.election (send1->qualified_root ())) != nullptr);
	ASSERT_EQ (1, election->blocks ().size ());

	auto vote1 = nano::test::make_final_vote (nano::dev::genesis_key, { send1->hash () });
	ASSERT_EQ (nano::vote_code::vote, node1.vote_router.vote (vote1).at (send1->hash ()));

	auto channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (channel, nullptr);

	auto vote2 = nano::test::make_final_vote (key1, { send1->hash () });
	node1.rep_crawler.force_process (vote2, channel);

	ASSERT_FALSE (election->confirmed ());

	// Modify online_m for online_reps to more than is available, this checks that voting below updates it to current online reps.
	node1.online_reps.force_online_weight (node_config.online_weight_minimum.number () + 20);
	ASSERT_EQ (nano::vote_code::vote, node1.vote_router.vote (vote2).at (send1->hash ()));
	ASSERT_TIMELY (5s, election->confirmed ());
	ASSERT_NE (nullptr, node1.block (send1->hash ()));
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
				 .build ();

	ASSERT_TRUE (nano::test::process (node1, { send1 }));
	nano::test::confirm (node1.ledger, send1);

	node1.stats.clear ();

	// Create a block that should be staying in AEC but not get confirmed
	auto send2 = builder.make_block ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (node1.balance (nano::dev::genesis_key.pub) - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	ASSERT_TRUE (nano::test::process (node1, { send2 }));
	ASSERT_TIMELY (5s, node1.active.active (*send2));

	// Ensure votes are broadcasted in continuous manner
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::election, nano::stat::detail::broadcast_vote) >= 5);
}

namespace
{
// Election filled to capacity with forks, one of them evicted by an incoming fork backed by cached rep weight
struct eviction_fixture
{
	std::shared_ptr<nano::election> election;
	std::vector<std::shared_ptr<nano::block>> forks; // The ten original forks, election started on forks[0]
	std::shared_ptr<nano::block> fork_new; // The incoming fork whose admission caused the eviction
	std::shared_ptr<nano::block> evicted; // The fork evicted to make room
	nano::account evicted_voter; // Zero-weight representative retaining the evicted fork's route
};

// Fill a fresh election with ten genesis-chain forks, then evict one by admitting an eleventh fork carrying cached vote weight from `rep_key`
eviction_fixture setup_evicted_fork (nano::test::system & system, nano::node & node, nano::keypair const & rep_key)
{
	nano::state_block_builder builder;

	// A second representative with just enough weight to drive an eviction
	auto const rep_weight = node.minimum_principal_weight ();
	auto send_rep = builder.make_block ()
					.account (nano::dev::genesis_key.pub)
					.previous (nano::dev::genesis->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (nano::dev::constants.genesis_amount - rep_weight)
					.link (rep_key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (nano::dev::genesis->hash ()))
					.build ();
	auto open_rep = builder.make_block ()
					.account (rep_key.pub)
					.previous (0)
					.representative (rep_key.pub)
					.balance (rep_weight)
					.link (send_rep->hash ())
					.sign (rep_key.prv, rep_key.pub)
					.work (*system.work.generate (rep_key.pub))
					.build ();
	EXPECT_EQ (nano::block_status::progress, node.process (send_rep));
	EXPECT_EQ (nano::block_status::progress, node.process (open_rep));
	// Cement the setup chain so the fork elections are next in line for activation
	nano::test::confirm (node.ledger, send_rep);
	nano::test::confirm (node.ledger, open_rep);

	eviction_fixture fixture;

	// Ten forks of the same root fill the election to its block limit
	nano::keypair destination;
	auto const balance = nano::dev::constants.genesis_amount - rep_weight;
	auto make_fork = [&] (nano::uint128_t amount) {
		return builder.make_block ()
		.account (nano::dev::genesis_key.pub)
		.previous (send_rep->hash ())
		.representative (nano::dev::genesis_key.pub)
		.balance (balance - amount)
		.link (destination.pub)
		.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
		.work (*system.work.generate (send_rep->hash ()))
		.build ();
	};
	for (auto i = 0; i < 10; ++i)
	{
		fixture.forks.push_back (make_fork (1 + i));
	}
	node.process_active (fixture.forks[0]);
	EXPECT_TIMELY (5s, (fixture.election = node.active.election (fixture.forks[0]->qualified_root ())) != nullptr);
	for (auto i = 1; i < 10; ++i)
	{
		node.process_active (fixture.forks[i]);
	}
	EXPECT_TIMELY (5s, fixture.election->blocks ().size () == 10);

	// Give the lowest-hash non-winner a vote without changing its zero tally
	auto expected_evicted = *std::min_element (fixture.forks.begin () + 1, fixture.forks.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs->hash () < rhs->hash ();
	});
	nano::keypair evicted_voter;
	fixture.evicted_voter = evicted_voter.pub;
	EXPECT_EQ (nano::vote_code::vote, fixture.election->vote (evicted_voter.pub, nano::vote::timestamp_min, expected_evicted->hash (), nano::vote_source::live));

	// An eleventh fork backed by cached rep weight evicts the lowest-hash zero-weight fork
	fixture.fork_new = make_fork (100);
	auto cached_vote = nano::test::make_vote (rep_key, { fixture.fork_new }, 0, 0);
	node.vote_router.vote (cached_vote); // No election holds the hash yet, the vote parks in the vote cache
	node.process_active (fixture.fork_new);
	EXPECT_TIMELY (5s, fixture.election->contains_block (fixture.fork_new->hash ()));

	// Exactly one of the original forks was evicted to make room, never the winner
	auto const held = fixture.election->blocks ();
	for (auto const & fork : fixture.forks)
	{
		if (!held.contains (fork->hash ()))
		{
			EXPECT_EQ (nullptr, fixture.evicted);
			fixture.evicted = fork;
		}
	}
	EXPECT_NE (nullptr, fixture.evicted);
	EXPECT_NE (fixture.forks[0], fixture.evicted);
	EXPECT_EQ (expected_evicted, fixture.evicted);
	return fixture;
}
}

// Eviction drops only the block: the vote route survives, so a representative voting for the evicted fork keeps updating this election instead of falling back to the vote cache
TEST (election, evicted_fork_keeps_receiving_votes)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::keypair rep_key;
	auto fixture = setup_evicted_fork (system, node, rep_key);

	// The route for the evicted fork survives the eviction
	ASSERT_TRUE (node.vote_router.contains (fixture.evicted->hash ()));

	// A vote for the evicted fork still reaches the election and is recorded against the rep
	auto vote = nano::test::make_vote (nano::dev::genesis_key, { fixture.evicted }, 0, 0);
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (vote).at (fixture.evicted->hash ()));
	ASSERT_EQ (fixture.evicted->hash (), fixture.election->votes ().at (nano::dev::genesis_key.pub).hash);
}

/*
 * An evicted hash keeps its route only while at least one representative's current vote names it.
 * Moving the last such vote removes the route; a held block keeps its route without current vote support.
 */
TEST (election, evicted_route_released_after_last_vote_moves)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::keypair rep_key;
	auto fixture = setup_evicted_fork (system, node, rep_key);

	ASSERT_TRUE (node.vote_router.contains (fixture.evicted->hash ()));
	// Add a second current vote for the evicted hash
	ASSERT_EQ (nano::vote_code::vote, fixture.election->vote (nano::dev::genesis_key.pub, nano::vote::timestamp_min, fixture.evicted->hash (), nano::vote_source::cache));

	// Moving one representative away leaves the route supported by the other
	ASSERT_EQ (nano::vote_code::vote, fixture.election->vote (fixture.evicted_voter, nano::vote::timestamp_min + 1, fixture.fork_new->hash (), nano::vote_source::cache));
	ASSERT_TRUE (node.vote_router.contains (fixture.evicted->hash ()));

	// Moving the last representative away releases the route
	ASSERT_EQ (nano::vote_code::vote, fixture.election->vote (nano::dev::genesis_key.pub, nano::vote::timestamp_min + 1, fixture.fork_new->hash (), nano::vote_source::cache));
	ASSERT_FALSE (node.vote_router.contains (fixture.evicted->hash ()));

	// Moving the sole vote away from a held block leaves its route intact
	ASSERT_EQ (nano::vote_code::vote, fixture.election->vote (nano::dev::genesis_key.pub, nano::vote::timestamp_min + 2, fixture.forks[0]->hash (), nano::vote_source::cache));
	ASSERT_EQ (nano::vote_code::vote, fixture.election->vote (nano::dev::genesis_key.pub, nano::vote::timestamp_min + 3, fixture.fork_new->hash (), nano::vote_source::cache));
	ASSERT_TRUE (node.vote_router.contains (fixture.forks[0]->hash ()));
}

/*
 * Repeated replacement must not retain every route an election has created.
 * Route count stays bounded by held blocks plus evicted hashes still named by current votes.
 */
TEST (election, evicted_routes_do_not_accumulate)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::keypair rep_key;
	auto fixture = setup_evicted_fork (system, node, rep_key);
	auto reference = std::dynamic_pointer_cast<nano::state_block> (fixture.fork_new);
	ASSERT_NE (nullptr, reference);

	// Track every block seen while repeatedly replacing a full ballot
	std::vector<std::shared_ptr<nano::block>> observed = fixture.forks;
	observed.push_back (fixture.fork_new);
	nano::state_block_builder builder;
	// Each fork carries enough cached weight to enter and force another replacement
	for (uint64_t i = 0; i < 20; ++i)
	{
		auto fork = builder.make_block ()
					.account (reference->account_field ().value ())
					.previous (reference->previous ())
					.representative (reference->representative_field ().value ())
					.balance (reference->balance_field ().value ().number () - 1 - i)
					.link (reference->link_field ().value ())
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (0)
					.build ();
		node.vote_cache.insert (nano::test::make_vote (rep_key, { fork }, i + 1, 0));
		ASSERT_TRUE (fixture.election->publish (fork));
		observed.push_back (fork);
	}

	// Only held blocks and the one vote-backed evicted fork retain routes
	auto const route_count = std::count_if (observed.begin (), observed.end (), [&node] (auto const & block) {
		return node.vote_router.contains (block->hash ());
	});
	ASSERT_EQ (fixture.election->block_count () + 1, route_count);
}

// Erasing an election removes the vote routes of its evicted forks along with those of the held blocks, so no route outlives the election it points to
TEST (election, erase_disconnects_evicted_forks)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::keypair rep_key;
	auto fixture = setup_evicted_fork (system, node, rep_key);

	// Held and evicted hashes are all routed while the election lives
	ASSERT_TRUE (node.vote_router.contains (fixture.evicted->hash ()));
	ASSERT_TRUE (node.vote_router.contains (fixture.fork_new->hash ()));

	ASSERT_TRUE (node.active.erase (fixture.forks[0]->qualified_root ()));
	for (auto const & fork : fixture.forks)
	{
		ASSERT_FALSE (node.vote_router.contains (fork->hash ()));
	}
	ASSERT_FALSE (node.vote_router.contains (fixture.fork_new->hash ()));
}

/*
 * A representative can finalize a fork while it is evicted: the retained route delivers the final vote to the election, where it accumulates behind the unheld hash without confirming anything, since only a held block can win.
 * When the finalized block then returns, its retained weight readmits it and the election re-evaluates immediately, reaching final quorum on the returned winner.
 * This is the votes-before-block principle inside a live election: consensus completes the moment the missing block arrives.
 */
TEST (election, evicted_fork_readmission_confirms)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto & node = *system.add_node (node_config);
	nano::keypair rep_key;
	auto fixture = setup_evicted_fork (system, node, rep_key);

	// The rep finalizes the evicted fork; the vote is recorded, but nothing confirms while the block is unheld
	auto final_vote = nano::test::make_final_vote (nano::dev::genesis_key, { fixture.evicted });
	ASSERT_EQ (nano::vote_code::vote, node.vote_router.vote (final_vote).at (fixture.evicted->hash ()));
	ASSERT_EQ (fixture.evicted->hash (), fixture.election->votes ().at (nano::dev::genesis_key.pub).hash);
	WAIT (500ms);
	ASSERT_FALSE (fixture.election->confirmed ());

	// The finalized block returns: its retained weight readmits it and the election confirms it on the spot
	node.process_active (fixture.evicted);
	ASSERT_TIMELY (5s, fixture.election->confirmed ());
	ASSERT_EQ (fixture.evicted->hash (), fixture.election->winner ()->hash ());
}
