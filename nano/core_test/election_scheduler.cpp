#include <nano/lib/blocks.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/election.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

TEST (election_scheduler, construction)
{
	nano::test::system system{ 1 };
}

TEST (election_scheduler, activate_one_timely)
{
	nano::test::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	system.nodes[0]->ledger.process (system.nodes[0]->store.tx_begin_write (), send1);
	system.nodes[0]->scheduler.priority.activate (nano::dev::genesis_key.pub, system.nodes[0]->store.tx_begin_read ());
	ASSERT_TIMELY (5s, system.nodes[0]->active.election (send1->qualified_root ()));
}

TEST (election_scheduler, activate_one_flush)
{
	nano::test::system system{ 1 };
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				 .link (nano::dev::genesis_key.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	system.nodes[0]->ledger.process (system.nodes[0]->store.tx_begin_write (), send1);
	system.nodes[0]->scheduler.priority.activate (nano::dev::genesis_key.pub, system.nodes[0]->store.tx_begin_read ());
	ASSERT_TIMELY (5s, system.nodes[0]->active.election (send1->qualified_root ()));
}

/**
 * Tests that the election scheduler and the active transactions container (AEC)
 * work in sync with regards to the node configuration value "active_elections_size".
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
	nano::test::system system{};

	nano::node_config config = system.default_config ();
	config.active_elections_size = 1;
	config.frontiers_confirmation = nano::frontiers_confirmation_mode::disabled;

	auto & node = *system.add_node (config);
	nano::state_block_builder builder{};
	nano::keypair key{};

	// Activating accounts depends on confirmed dependencies. First, prepare 2 accounts
	auto send = builder.make_block ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.link (key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Gxrb_ratio)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));
	node.process_confirmed (nano::election_status{ send });

	auto receive = builder.make_block ()
				   .account (key.pub)
				   .previous (0)
				   .representative (key.pub)
				   .link (send->hash ())
				   .balance (nano::Gxrb_ratio)
				   .sign (key.prv, key.pub)
				   .work (*system.work.generate (key.pub))
				   .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (receive));
	node.process_confirmed (nano::election_status{ receive });

	// Second, process two eligible transactions
	auto block1 = builder.make_block ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (send->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .link (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 2 * nano::Gxrb_ratio)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*system.work.generate (send->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (block1));

	// There is vacancy so it should be inserted
	node.scheduler.priority.activate (nano::dev::genesis_key.pub, node.store.tx_begin_read ());
	std::shared_ptr<nano::election> election{};
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
	ASSERT_EQ (nano::block_status::progress, node.process (block2));

	// There is no vacancy so it should stay queued
	node.scheduler.priority.activate (key.pub, node.store.tx_begin_read ());
	ASSERT_TIMELY_EQ (5s, node.scheduler.priority.size (), 1);
	ASSERT_EQ (node.active.election (block2->qualified_root ()), nullptr);

	// Election confirmed, next in queue should begin
	election->force_confirm ();
	ASSERT_TIMELY (5s, node.active.election (block2->qualified_root ()) != nullptr);
	ASSERT_TRUE (node.scheduler.priority.empty ());
}
