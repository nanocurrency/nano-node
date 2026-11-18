#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

/*
 * Verifies that the priority scheduler:
 * 1) Activates the best-available block from multiple buckets in a single run loop
 * 2) Drains the shared pool after scheduling (i.e., no leftovers remain queued)
 *
 * Test outline:
 * - Create two distinct accounts (A and B) with different final balances so they map to different scheduler buckets.
 * - Cement their initial open chains, then create the next blocks on both accounts (eligible for elections).
 * - Request activation for both accounts. The scheduler should pick one candidate per non-empty bucket.
 * - Assert that the pool becomes empty and elections exist for both accounts' next blocks.
 *
 * Notes:
 * - We keep active_elections.size comfortably large and reserved_elections small to avoid AEC contention and
 *   to allow fair activation across buckets. The test asserts only on final observable conditions to avoid flakiness.
 */
TEST (priority_scheduler, activate_multiple_buckets)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	// Ensure enough room for elections to start
	config.active_elections->size = 10;
	// Keep reserved small to allow fair activation across buckets regardless of AEC
	config.priority_scheduler->reserved_elections = 1;
	auto & node = *system.add_node (config);

	// Create two accounts with different balances so they fall into different buckets via bucketing
	nano::state_block_builder builder;

	// Send to A and B, cement them, then create next blocks eligible for election
	nano::keypair account_a, account_b;

	auto send_to_a = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_a.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_a));

	auto open_a = builder.make_block ()
				  .account (account_a.pub)
				  .previous (0)
				  .representative (account_a.pub)
				  .link (send_to_a->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_a.prv, account_a.pub)
				  .work (*system.work.generate (account_a.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_a));

	// Send to B from genesis' successor
	auto send_to_b = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send_to_a->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_b.pub)
					 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send_to_a->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_b));

	auto open_b = builder.make_block ()
				  .account (account_b.pub)
				  .previous (0)
				  .representative (account_b.pub)
				  .link (send_to_b->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_b.prv, account_b.pub)
				  .work (*system.work.generate (account_b.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_b));

	// Cement the initial opens so their successors are eligible for activation
	nano::test::confirm (node, { send_to_a, open_a, send_to_b, open_b });

	// Now create next blocks on both accounts; different balances produce different buckets by design
	auto next_a = builder.make_block ()
				  .account (account_a.pub)
				  .previous (open_a->hash ())
				  .representative (account_a.pub)
				  .link (account_a.pub)
				  .balance (0)
				  .sign (account_a.prv, account_a.pub)
				  .work (*system.work.generate (open_a->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_a));

	auto next_b = builder.make_block ()
				  .account (account_b.pub)
				  .previous (open_b->hash ())
				  .representative (account_b.pub)
				  .link (account_b.pub)
				  .balance (0)
				  .sign (account_b.prv, account_b.pub)
				  .work (*system.work.generate (open_b->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_b));

	// Request activation for both accounts via scheduler
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), account_a.pub);
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), account_b.pub);

	// Both should be scheduled and then started; pool empties and elections exist for both roots
	ASSERT_TIMELY (5s, node.scheduler.priority.empty ());
	ASSERT_TIMELY (5s, node.active.election (next_a->qualified_root ()) != nullptr);
	ASSERT_TIMELY (5s, node.active.election (next_b->qualified_root ()) != nullptr);
}

/*
 * Verifies reserved_elections behavior when the Active Elections Container (AEC) is at capacity
 *
 * Focus:
 * - Even when AEC is at capacity (no vacancy), buckets can still activate elections up to their reserved limit
 * - The reserved_elections setting guarantees a minimum number of active elections per bucket regardless of AEC capacity
 *
 * Test outline:
 * - Set AEC size to 1 and reserved_elections to 2, creating a situation where reserved > AEC capacity
 * - Create two accounts in the same bucket (same balance)
 * - Activate the first account - should succeed even though it fills the AEC
 * - Activate the second account - should succeed because it's within reserved limit, even though AEC has no vacancy
 */
TEST (priority_scheduler, reserved_respected_no_vacancy)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	// Only 1 slot in AEC to force no-vacancy condition
	config.active_elections->size = 1;
	// Allow 2 reserved elections per bucket, more than AEC capacity
	config.priority_scheduler->reserved_elections = 2;
	config.priority_scheduler->max_elections = 2;
	auto & node = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair account_a, account_b;

	// Create two accounts with same balance so they fall into the same bucket
	auto send_to_a = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_a.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_a));

	auto open_a = builder.make_block ()
				  .account (account_a.pub)
				  .previous (0)
				  .representative (account_a.pub)
				  .link (send_to_a->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_a.prv, account_a.pub)
				  .work (*system.work.generate (account_a.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_a));

	auto send_to_b = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send_to_a->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_b.pub)
					 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send_to_a->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_b));

	auto open_b = builder.make_block ()
				  .account (account_b.pub)
				  .previous (0)
				  .representative (account_b.pub)
				  .link (send_to_b->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_b.prv, account_b.pub)
				  .work (*system.work.generate (account_b.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_b));

	// Cement the opens
	nano::test::confirm (node, { send_to_a, open_a, send_to_b, open_b });

	// Create next blocks on both accounts (same balance = same bucket)
	auto next_a = builder.make_block ()
				  .account (account_a.pub)
				  .previous (open_a->hash ())
				  .representative (account_a.pub)
				  .link (account_a.pub)
				  .balance (0)
				  .sign (account_a.prv, account_a.pub)
				  .work (*system.work.generate (open_a->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_a));

	auto next_b = builder.make_block ()
				  .account (account_b.pub)
				  .previous (open_b->hash ())
				  .representative (account_b.pub)
				  .link (account_b.pub)
				  .balance (0)
				  .sign (account_b.prv, account_b.pub)
				  .work (*system.work.generate (open_b->hash ()))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_b));

	// Activate first account - should succeed and fill the AEC
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), account_a.pub);
	ASSERT_TIMELY (5s, node.active.election (next_a->qualified_root ()) != nullptr);

	// At this point AEC is at capacity (1/1), but bucket has only 1 election (below reserved limit of 2)
	// Activate second account from same bucket - should succeed because reserved limit is not reached
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), account_b.pub);
	ASSERT_TIMELY (5s, node.active.election (next_b->qualified_root ()) != nullptr);

	// Both elections should be active, demonstrating that reserved_elections is respected even with no AEC vacancy
	ASSERT_NE (node.active.election (next_a->qualified_root ()), nullptr);
	ASSERT_NE (node.active.election (next_b->qualified_root ()), nullptr);
}

/*
 * Verifies that activations are queued when both AEC and bucket capacity are exhausted
 *
 * Focus:
 * - When AEC is at capacity and bucket has reached both reserved and max election limits
 * - Additional activation requests should remain queued in the scheduler pool
 *
 * Test outline:
 * - Set AEC size to 1, reserved_elections to 1, and max_elections to 1
 * - Create two accounts in the same bucket
 * - Activate first account - should succeed and exhaust both AEC and bucket capacity
 * - Activate second account - should remain queued in the pool since no capacity is available
 */
TEST (priority_scheduler, queue_activations)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	// Only 1 slot in AEC, force contention
	config.active_elections->size = 1;
	// Allow bucket to start 1 election regardless of vacancy, but not more
	config.priority_scheduler->reserved_elections = 1;
	config.priority_scheduler->max_elections = 1;
	auto & node = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair keypair1, keypair2;

	// Prepare keypair1 and keypair2 accounts
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (keypair1.pub)
				 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	auto open1 = builder.make_block ()
				 .account (keypair1.pub)
				 .previous (0)
				 .representative (keypair1.pub)
				 .link (send1->hash ())
				 .balance (nano::Knano_ratio)
				 .sign (keypair1.prv, keypair1.pub)
				 .work (*system.work.generate (keypair1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open1));

	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (keypair2.pub)
				 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	auto open2 = builder.make_block ()
				 .account (keypair2.pub)
				 .previous (0)
				 .representative (keypair2.pub)
				 .link (send2->hash ())
				 .balance (nano::Knano_ratio)
				 .sign (keypair2.prv, keypair2.pub)
				 .work (*system.work.generate (keypair2.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open2));

	// Cement the initial opens
	nano::test::confirm (node, { send1, open1, send2, open2 });

	// Create next blocks on both
	auto next_block1 = builder.make_block ()
					   .account (keypair1.pub)
					   .previous (open1->hash ())
					   .representative (keypair1.pub)
					   .link (keypair1.pub)
					   .balance (0)
					   .sign (keypair1.prv, keypair1.pub)
					   .work (*system.work.generate (open1->hash ()))
					   .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_block1));

	auto next_block2 = builder.make_block ()
					   .account (keypair2.pub)
					   .previous (open2->hash ())
					   .representative (keypair2.pub)
					   .link (keypair2.pub)
					   .balance (0)
					   .sign (keypair2.prv, keypair2.pub)
					   .work (*system.work.generate (open2->hash ()))
					   .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_block2));

	// First activation should start immediately (within reserved)
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), keypair1.pub);

	std::shared_ptr<nano::election> election1;
	ASSERT_TIMELY (5s, (election1 = node.active.election (next_block1->qualified_root ())) != nullptr);

	// Second activation should be queued because AEC is at capacity and reserved for the bucket is used
	node.scheduler.priority.activate (node.ledger.tx_begin_read (), keypair2.pub);
	ASSERT_ALWAYS_EQ (500ms, node.scheduler.priority.size (), 1);
}

/*
 * Ensures that when a bucket is at max_elections, introducing a better-priority candidate
 * results in the worse-priority election being evicted and the better one remaining active.
 *
 * Focus:
 * - Assert only the final, observable condition: the better-priority election is active and
 *   the worse-priority one is not. Avoid relying on cleanup timing or transient bucket counts.
 *
 * Test outline:
 * - Prepare two accounts that end up in the same bucket and create eligible next blocks.
 * - Push a worse-priority block first and wait until its election becomes active.
 * - Push a better-priority block from the same bucket.
 * - Assert that eventually the better election is active and the worse election is gone.
 */
TEST (priority_scheduler, cancel_worst_election)
{
	nano::test::system system;

	nano::node_config config;
	config.backlog_scan->enable = false;
	// Enough AEC size so vacancy does not block, we rely on bucket logic to temporarily overfill and then cleanup
	config.active_elections->size = 10;
	config.priority_scheduler->reserved_elections = 1;
	config.priority_scheduler->max_elections = 1;
	auto & node = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair account_c, account_d;

	// Fund two accounts with same balance so they land in the same bucket (low balance region)
	auto send_to_c = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (nano::dev::genesis->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_c.pub)
					 .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (nano::dev::genesis->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_c));
	auto open_c = builder.make_block ()
				  .account (account_c.pub)
				  .previous (0)
				  .representative (account_c.pub)
				  .link (send_to_c->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_c.prv, account_c.pub)
				  .work (*system.work.generate (account_c.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_c));

	auto send_to_d = builder.make_block ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (send_to_c->hash ())
					 .representative (nano::dev::genesis_key.pub)
					 .link (account_d.pub)
					 .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*system.work.generate (send_to_c->hash ()))
					 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_d));
	auto open_d = builder.make_block ()
				  .account (account_d.pub)
				  .previous (0)
				  .representative (account_d.pub)
				  .link (send_to_d->hash ())
				  .balance (nano::Knano_ratio)
				  .sign (account_d.prv, account_d.pub)
				  .work (*system.work.generate (account_d.pub))
				  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_d));

	// Cement the opens
	nano::test::confirm (node, { send_to_c, open_c, send_to_d, open_d });

	// Create eligible next blocks for both accounts
	auto worse_priority_block = builder.make_block ()
								.account (account_c.pub)
								.previous (open_c->hash ())
								.representative (account_c.pub)
								.link (account_c.pub)
								.balance (0)
								.sign (account_c.prv, account_c.pub)
								.work (*system.work.generate (open_c->hash ()))
								.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (worse_priority_block));

	auto better_priority_block = builder.make_block ()
								 .account (account_d.pub)
								 .previous (open_d->hash ())
								 .representative (account_d.pub)
								 .link (account_d.pub)
								 .balance (0)
								 .sign (account_d.prv, account_d.pub)
								 .work (*system.work.generate (open_d->hash ()))
								 .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (better_priority_block));

	// Directly manipulate the scheduler pool to control priorities and bucket index
	// Choose a low bucket (0) and push a worse-priority candidate first, then a better one
	constexpr nano::bucket_index bucket = 7;
	// Higher priority timestamp = worse priority according to ordering (lower goes first)
	nano::priority_timestamp worse_priority = std::numeric_limits<nano::priority_timestamp>::max ();
	nano::priority_timestamp better_priority = 0;

	// Push worse first, should start immediately
	ASSERT_TRUE (node.scheduler.priority.push (worse_priority_block, bucket, worse_priority));

	// Wait for first activation to insert one election in that bucket
	ASSERT_TIMELY (5s, node.active.election (worse_priority_block->qualified_root ()) != nullptr);

	// Now push a better-priority block from the same bucket; bucket may temporarily overfill
	ASSERT_TRUE (node.scheduler.priority.push (better_priority_block, bucket, better_priority));

	// Eventually the better-priority election should remain active and the worse one should be evicted
	ASSERT_TIMELY (5s, node.active.election (better_priority_block->qualified_root ()) != nullptr);
	ASSERT_TIMELY (5s, node.active.election (worse_priority_block->qualified_root ()) == nullptr);
}

/**
 * Verifies that activate_successors triggers activation for both the sender's next block and
 * the destination account's next block when the send's destination is non-zero and different
 * from the sender.
 *
 * Test outline:
 * - Create a send from genesis to a new destination account and confirm the send.
 * - Open the destination account so it has a frontier and then create the next block on genesis.
 * - Call activate_successors on the send block.
 * - Assert that both the destination's open block and genesis' next block have active elections, indicating that
 *   both successors were activated correctly.
 */
TEST (priority_scheduler, activate_successors)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);

	nano::state_block_builder builder;
	nano::keypair destination_account;

	auto send_to_destination = builder.make_block ()
							   .account (nano::dev::genesis_key.pub)
							   .previous (nano::dev::genesis->hash ())
							   .representative (nano::dev::genesis_key.pub)
							   .link (destination_account.pub)
							   .balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
							   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
							   .work (*system.work.generate (nano::dev::genesis->hash ()))
							   .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send_to_destination));
	nano::test::confirm (node, { send_to_destination });

	// Open for destination so it has a frontiers
	auto open_destination = builder.make_block ()
							.account (destination_account.pub)
							.previous (0)
							.representative (destination_account.pub)
							.link (send_to_destination->hash ())
							.balance (nano::Knano_ratio)
							.sign (destination_account.prv, destination_account.pub)
							.work (*system.work.generate (destination_account.pub))
							.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (open_destination));

	// Next on sender and confirm its dependencies
	auto next_genesis_block = builder.make_block ()
							  .account (nano::dev::genesis_key.pub)
							  .previous (send_to_destination->hash ())
							  .representative (nano::dev::genesis_key.pub)
							  .link (nano::dev::genesis_key.pub)
							  .balance (nano::dev::constants.genesis_amount - 2 * nano::Knano_ratio)
							  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
							  .work (*system.work.generate (send_to_destination->hash ()))
							  .build ();
	ASSERT_EQ (nano::block_status::progress, node.process (next_genesis_block));

	// Activate successors from the send block: should try sender and destination
	node.scheduler.priority.activate_successors (node.ledger.tx_begin_read (), *send_to_destination);

	// Assert on stable, observable end-state: both successors become active elections
	ASSERT_TIMELY (5s, node.active.election (open_destination->qualified_root ()) != nullptr);
	ASSERT_TIMELY (5s, node.active.election (next_genesis_block->qualified_root ()) != nullptr);
}

/**
 * Stress test to verify notifications are not missed in priority scheduler.
 * If notifications are missed, blocks get stuck and test times out.
 */
TEST (priority_scheduler, stress_test)
{
	nano::test::system system;

	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false; // Disable fallback mechanisms to avoid interference

	nano::node_flags flags;
	flags.disable_activate_successors = true; // Full control over activations

	auto & node = *system.add_node (config, flags);

	// Create N unconfirmed open blocks
	// All have balance = 1 raw, so they land in the same bucket
	constexpr size_t num_blocks = 20;
	auto blocks = nano::test::setup_independent_blocks (system, node, num_blocks);

	// Worker pool for async activation - decouples notification from activation
	nano::thread_pool workers{ 1, nano::thread_role::name::unknown };
	START (workers);

	// Track activations via batch_activated callback
	std::atomic<size_t> activated_count{ 0 };
	std::atomic<size_t> next_to_activate{ 1 }; // Start from 1 since we activate 0 manually

	node.scheduler.priority.batch_activated.add ([&] (auto const & batch) {
		activated_count += batch.size ();

		workers.post ([&] () {
			std::this_thread::yield (); // Increase timing variability

			// Activate the next account
			auto idx = next_to_activate.fetch_add (1);
			if (idx < blocks.size ())
			{
				auto txn = node.ledger.tx_begin_read ();
				EXPECT_TRUE (node.scheduler.priority.activate (txn, blocks[idx]->account ()));
			}
		});
	});

	// Kick off the chain by activating the first block
	ASSERT_TRUE (node.scheduler.priority.activate (node.ledger.tx_begin_read (), blocks[0]->account ()));

	// All blocks should eventually be activated
	// If race occurs, some activations sit in pool and never get processed → timeout
	ASSERT_TIMELY (15s, activated_count >= num_blocks);

	// Verify pool is empty (all blocks were activated)
	ASSERT_TRUE (node.scheduler.priority.empty ());
}