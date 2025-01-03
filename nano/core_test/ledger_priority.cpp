#include <nano/lib/blocks.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/test_common/ledger_context.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

// Test priority of genesis block
TEST (ledger_priority, genesis_priority)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();

	auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *nano::dev::genesis);
	ASSERT_EQ (nano::dev::constants.genesis_amount, priority_balance);
	ASSERT_EQ (0, priority_timestamp);
}

// Test priority of legacy blocks
TEST (ledger_priority, legacy_blocks_priority)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	// Create legacy send block
	nano::keypair key1;
	nano::block_builder builder;
	auto send = builder
				.send ()
				.previous (nano::dev::genesis->hash ())
				.destination (key1.pub)
				.balance (100)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send));

	// Check send priority - should use max of current and previous balance
	auto const [send_balance, send_timestamp] = ledger.block_priority (transaction, *send);
	ASSERT_EQ (nano::dev::constants.genesis_amount, send_balance);
	ASSERT_EQ (nano::dev::genesis->sideband ().timestamp, send_timestamp);

	// Create legacy open/receive block
	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (key1.pub)
				.account (key1.pub)
				.sign (key1.prv, key1.pub)
				.work (*pool.generate (key1.pub))
				.build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, open));

	// Check open priority - should use current balance
	auto const [open_balance, open_timestamp] = ledger.block_priority (transaction, *open);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, open_balance);
	ASSERT_EQ (open->sideband ().timestamp, open_timestamp);
}

// Test priority of a send state block
TEST (ledger_priority, send_priority)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	nano::keypair key1;
	nano::block_builder builder;

	// Create state send block
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (100)
				.link (key1.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send));

	// Check priority
	auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *send);
	ASSERT_EQ (nano::dev::constants.genesis_amount, priority_balance);
	ASSERT_EQ (nano::dev::genesis->sideband ().timestamp, priority_timestamp);
}

// Test priority of a full balance state send
TEST (ledger_priority, full_balance_send)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	nano::keypair key1;
	nano::block_builder builder;

	// Create state send block that sends full balance
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (0)
				.link (key1.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send));

	// Check priority - should use previous balance since current is 0
	auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *send);
	ASSERT_EQ (nano::dev::constants.genesis_amount, priority_balance);
	ASSERT_EQ (nano::dev::genesis->sideband ().timestamp, priority_timestamp);
}

// Test priority of state blocks with multiple operations
TEST (ledger_priority, sequential_blocks)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	nano::keypair key1;
	nano::block_builder builder;

	// First state send
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send1));

	// State open
	auto open = builder
				.state ()
				.account (key1.pub)
				.previous (0)
				.representative (key1.pub)
				.balance (100)
				.link (send1->hash ())
				.sign (key1.prv, key1.pub)
				.work (*pool.generate (key1.pub))
				.build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, open));

	// Second state send
	auto send2 = builder
				 .state ()
				 .account (key1.pub)
				 .previous (open->hash ())
				 .representative (key1.pub)
				 .balance (50)
				 .link (nano::dev::genesis_key.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (open->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send2));

	// Check priorities follow the chain
	auto const [priority_balance1, timestamp1] = ledger.block_priority (transaction, *send1);
	auto const [priority_balance2, timestamp2] = ledger.block_priority (transaction, *open);
	auto const [priority_balance3, timestamp3] = ledger.block_priority (transaction, *send2);

	ASSERT_EQ (nano::dev::constants.genesis_amount, priority_balance1);
	ASSERT_EQ (100, priority_balance2);
	ASSERT_EQ (100, priority_balance3); // Max of current (50) and previous (100)

	ASSERT_EQ (nano::dev::genesis->sideband ().timestamp, timestamp1); // genesis account
	// Opening account must have equal or greater timestamp than sending counterpart.
	ASSERT_GE (timestamp2, send1->sideband ().timestamp); // key1 account
	ASSERT_EQ (open->sideband ().timestamp, timestamp3); // key1 account
}

// Test priority after rolling back state blocks
TEST (ledger_priority, block_rollback)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	nano::keypair key1;
	nano::block_builder builder;

	// First state send
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send1));

	// Get priority before rollback
	auto const [priority_before, timestamp_before] = ledger.block_priority (transaction, *send1);

	// Rollback the send
	ASSERT_FALSE (ledger.rollback (transaction, send1->hash ()));

	// Create new state send with different amount
	auto send2 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send2));

	// Check priority after rollback and new block
	auto const [priority_after, timestamp_after] = ledger.block_priority (transaction, *send2);

	// Priorities should be the same since both use genesis as previous
	ASSERT_EQ (priority_before, priority_after);
	ASSERT_EQ (timestamp_before, timestamp_after);
}

// Test priority with state block fork
TEST (ledger_priority, block_fork)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	auto transaction = ledger.tx_begin_write ();

	nano::keypair key1, key2;
	nano::block_builder builder;

	// First state send
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send1));

	// Create two competing state sends
	auto send2a = builder
				  .state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 200)
				  .link (key1.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();

	auto send2b = builder
				  .state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 150)
				  .link (key2.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();

	// Process first fork
	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send2a));

	// Try to process second fork
	ASSERT_EQ (nano::block_status::fork, ledger.process (transaction, send2b));

	// Get priority of first fork
	auto const [priority_a, timestamp_a] = ledger.block_priority (transaction, *send2a);

	// Rollback first fork
	ASSERT_FALSE (ledger.rollback (transaction, send2a->hash ()));

	// Process second fork
	ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, send2b));

	// Check priority of second fork
	auto const [priority_b, timestamp_b] = ledger.block_priority (transaction, *send2b);

	// Both should use send1's balance as previous
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, priority_a);
	ASSERT_EQ (nano::dev::constants.genesis_amount - 100, priority_b);
	// Both should use send1's timestamp
	ASSERT_EQ (send1->sideband ().timestamp, timestamp_a);
	ASSERT_EQ (send1->sideband ().timestamp, timestamp_b);
}