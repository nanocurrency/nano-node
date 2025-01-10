#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/test_common/chains.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
celerix::keypair & keyzero ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key0 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key1 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key2 ()
{
	static celerix::keypair result;
	return result;
}
celerix::keypair & key3 ()
{
	static celerix::keypair result;
	return result;
}
std::shared_ptr<celerix::state_block> & blockzero ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (keyzero ().pub)
						 .previous (0)
						 .representative (keyzero ().pub)
						 .balance (0)
						 .link (0)
						 .sign (keyzero ().prv, keyzero ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block0 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key0 ().pub)
						 .previous (0)
						 .representative (key0 ().pub)
						 .balance (celerix::Kcelerix_ratio)
						 .link (0)
						 .sign (key0 ().prv, key0 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block1 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key1 ().pub)
						 .previous (0)
						 .representative (key1 ().pub)
						 .balance (celerix::celerix_ratio)
						 .link (0)
						 .sign (key1 ().prv, key1 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block2 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key2 ().pub)
						 .previous (0)
						 .representative (key2 ().pub)
						 .balance (celerix::Kcelerix_ratio)
						 .link (0)
						 .sign (key2 ().prv, key2 ().pub)
						 .work (0)
						 .build ();
	return result;
}
std::shared_ptr<celerix::state_block> & block3 ()
{
	celerix::block_builder builder;
	static auto result = builder
						 .state ()
						 .account (key3 ().pub)
						 .previous (0)
						 .representative (key3 ().pub)
						 .balance (celerix::celerix_ratio)
						 .link (0)
						 .sign (key3 ().prv, key3 ().pub)
						 .work (0)
						 .build ();
	return result;
}
}

TEST (election_scheduler, activate_one_timely)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	node.ledger.process (node.ledger.tx_begin_write (), send1);
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, node.active.election (send1->qualified_root ()));
}

TEST (election_scheduler, activate_one_flush)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*system.work.generate (celerix::dev::genesis->hash ()))
				 .build ();
	node.ledger.process (node.ledger.tx_begin_write (), send1);
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	ASSERT_TIMELY (5s, node.active.election (send1->qualified_root ()));
}

/*
 * Tests that an optimistic election can be transitioned to a priority election.
 *
 * The test:
 * 1. Creates a chain of 2 blocks with an optimistic election for the second block
 * 2. Confirms the first block in the chain
 * 3. Attempts to start a priority election for the second block
 * 4. Verifies that the existing optimistic election is transitioned to priority
 * 5. Verifies a new vote is broadcast after the transition
 */
TEST (election_scheduler, transition_optimistic_to_priority)
{
	celerix::test::system system;
	celerix::node_config config = system.default_config ();
	config.optimistic_scheduler.gap_threshold = 1;
	config.enable_voting = true;
	config.hinted_scheduler.enable = false;
	config.network_params.network.vote_broadcast_interval = 15000ms;
	auto & node = *system.add_node (config);

	// Add representative
	const celerix::uint128_t rep_weight = celerix::Kcelerix_ratio * 100;
	celerix::keypair rep = celerix::test::setup_rep (system, node, rep_weight);
	system.wallet (0)->insert_adhoc (rep.prv);

	// Create a chain of blocks - and trigger an optimistic election for the last block
	const int howmany_blocks = 2;
	auto chains = celerix::test::setup_chains (system, node, /* single chain */ 1, howmany_blocks, celerix::dev::genesis_key, /* do not confirm */ false);
	auto & [account, blocks] = chains.front ();

	// Wait for optimistic election to start for last block
	auto const & block = blocks.back ();
	ASSERT_TIMELY (5s, node.vote_router.active (block->hash ()));
	auto election = node.active.election (block->qualified_root ());
	ASSERT_EQ (election->behavior (), celerix::election_behavior::optimistic);
	ASSERT_TIMELY_EQ (1s, 1, election->current_status ().status.vote_broadcast_count);

	// Confirm first block to allow upgrading second block's election
	celerix::test::confirm (node.ledger, blocks.at (howmany_blocks - 1));

	// Attempt to start priority election for second block
	node.active.insert (block, celerix::election_behavior::priority);

	// Verify priority transition
	ASSERT_EQ (election->behavior (), celerix::election_behavior::priority);
	ASSERT_EQ (1, node.stats.count (celerix::stat::type::active_elections, celerix::stat::detail::transition_priority));
	// Verify vote broadcast after transitioning
	ASSERT_TIMELY_EQ (1s, 2, election->current_status ().status.vote_broadcast_count);
	ASSERT_TRUE (node.active.active (*block));
}

/**
 * Tests that the election scheduler and the active transactions container (AEC)
 * work in sync with regards to the node configuration value "active_elections.size".
 *
 * The test sets up two forcefully cemented blocks -- a send on the genesis account and a receive on a second account.
 * It then creates two other blocks, each a successor to one of the previous two,
 * and processes them locally (without the node starting elections for them, but just saving them to disk).
 *
 * Elections for these latter two (B1 and B2) are started by the test code manually via `election_scheduler::activate`.
 * The test expects E1 to start right off and take its seat into the AEC.
 * E2 is expected not to start though (because the AEC is full), so B2 should be awaiting in the scheduler's queue.
 *
 * As soon as the test code manually confirms E1 (and thus evicts it out of the AEC),
 * it is expected that E2 begins and the scheduler's queue becomes empty again.
 */
TEST (election_scheduler, no_vacancy)
{
	celerix::test::system system;

	celerix::node_config config = system.default_config ();
	config.active_elections.size = 1;
	config.backlog_scan.enable = false;
	auto & node = *system.add_node (config);

	celerix::state_block_builder builder{};
	celerix::keypair key{};

	// Activating accounts depends on confirmed dependencies. First, prepare 2 accounts
	auto send = builder.make_block ()
				.account (celerix::dev::genesis_key.pub)
				.previous (celerix::dev::genesis->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.link (key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (send));
	node.confirming_set.add (send->hash ());

	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (0)
				   .representative (key.pub)
				   .link (send->hash ())
				   .balance (celerix::Kcelerix_ratio)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (key.pub))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (receive));
	node.confirming_set.add (receive->hash ());

	ASSERT_TIMELY (5s, celerix::test::confirmed (node, { send, receive }));

	// Second, process two eligible transactions
	auto block1 = builder.make_block ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (send->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .link (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (send->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (block1));

	// There is vacancy so it should be inserted
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	std::shared_ptr<celerix::election> election{};
	ASSERT_TIMELY (5s, (election = node.active.election (block1->qualified_root ())) != nullptr);

	auto block2 = builder.make_block ()
				  .account (key.pub)
				  .previous (receive->hash ())
				  .representative (key.pub)
				  .link (key.pub)
				  .balance (0)
				  .sign (key.prv, key.pub)
				  .work (*system.work.generate (receive->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (block2));

	// There is no vacancy so it should stay queued
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), key.pub);
	ASSERT_TIMELY_EQ (5s, node.scheduler.priority.size (), 1);
	ASSERT_EQ (node.active.election (block2->qualified_root ()), nullptr);

	// Election confirmed, next in queue should begin
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.election (block2->qualified_root ()) != nullptr);
	ASSERT_TRUE (node.scheduler.priority.empty ());
}

TEST (election_scheduler_bucket, construction)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::scheduler::priority_bucket_config bucket_config;
	celerix::scheduler::bucket bucket{ 0, bucket_config, node.active, node.stats };
	ASSERT_TRUE (bucket.empty ());
	ASSERT_EQ (0, bucket.size ());
}

TEST (election_scheduler_bucket, insert_one)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::scheduler::priority_bucket_config bucket_config;
	celerix::scheduler::bucket bucket{ 0, bucket_config, node.active, node.stats };
	ASSERT_FALSE (bucket.contains (block0 ()->hash ()));
	ASSERT_TRUE (bucket.push (1000, block0 ()));
	ASSERT_TRUE (bucket.contains (block0 ()->hash ()));
	ASSERT_FALSE (bucket.empty ());
	ASSERT_EQ (1, bucket.size ());
	auto blocks = bucket.blocks ();
	ASSERT_EQ (1, blocks.size ());
	ASSERT_EQ (block0 (), blocks.front ());
}

TEST (election_scheduler_bucket, insert_duplicate)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::scheduler::priority_bucket_config bucket_config;
	celerix::scheduler::bucket bucket{ 0, bucket_config, node.active, node.stats };
	ASSERT_TRUE (bucket.push (1000, block0 ()));
	ASSERT_FALSE (bucket.push (1000, block0 ()));
}

TEST (election_scheduler_bucket, insert_many)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::scheduler::priority_bucket_config bucket_config;
	celerix::scheduler::bucket bucket{ 0, bucket_config, node.active, node.stats };
	ASSERT_TRUE (bucket.push (2000, block0 ()));
	ASSERT_TRUE (bucket.push (1001, block1 ()));
	ASSERT_TRUE (bucket.push (1000, block2 ()));
	ASSERT_TRUE (bucket.push (900, block3 ()));
	ASSERT_FALSE (bucket.empty ());
	ASSERT_EQ (4, bucket.size ());
	auto blocks = bucket.blocks ();
	ASSERT_EQ (4, blocks.size ());
	// Ensure correct order
	ASSERT_EQ (blocks[0], block3 ());
	ASSERT_EQ (blocks[1], block2 ());
	ASSERT_EQ (blocks[2], block1 ());
	ASSERT_EQ (blocks[3], block0 ());
}

TEST (election_scheduler_bucket, max_blocks)
{
	celerix::test::system system;
	auto & node = *system.add_node ();

	celerix::scheduler::priority_bucket_config bucket_config{
		.max_blocks = 2
	};
	celerix::scheduler::bucket bucket{ 0, bucket_config, node.active, node.stats };
	ASSERT_TRUE (bucket.push (2000, block0 ()));
	ASSERT_TRUE (bucket.contains (block0 ()->hash ()));
	ASSERT_TRUE (bucket.push (900, block1 ()));
	ASSERT_TRUE (bucket.contains (block1 ()->hash ()));
	ASSERT_FALSE (bucket.push (3000, block2 ()));
	ASSERT_FALSE (bucket.contains (block2 ()->hash ()));
	ASSERT_TRUE (bucket.push (1001, block3 ())); // Evicts 2000
	ASSERT_FALSE (bucket.contains (block0 ()->hash ()));
	ASSERT_TRUE (bucket.push (1000, block0 ())); // Evicts 1001
	ASSERT_FALSE (bucket.contains (block3 ()->hash ()));
	ASSERT_EQ (2, bucket.size ());
	auto blocks = bucket.blocks ();
	ASSERT_EQ (2, blocks.size ());
	// Ensure correct order
	ASSERT_EQ (blocks[0], block1 ());
	ASSERT_EQ (blocks[1], block0 ());
}