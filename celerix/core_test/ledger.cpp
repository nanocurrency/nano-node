#include <celerix/lib/blocks.hpp>
#include <celerix/lib/logging.hpp>
#include <celerix/lib/numbers.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/make_store.hpp>
#include <celerix/node/scheduler/component.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/transport/inproc.hpp>
#include <celerix/node/vote_router.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/secure/vote.hpp>
#include <celerix/store/rocksdb/rocksdb.hpp>
#include <celerix/test_common/ledger_context.hpp>
#include <celerix/test_common/make_store.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <limits>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	if (celerix::rocksdb_config::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		GTEST_SKIP ();
	}
	auto ctx = celerix::test::ledger_empty ();
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_read ();
	celerix::account account;
	auto balance (ledger.any.account_balance (transaction, account));
	ASSERT_FALSE (balance);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, confirmed_unconfirmed_view)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & unconfirmed = ledger;
	auto & confirmed = ledger.confirmed;
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto balance = ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, balance);
	auto info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info);
	ASSERT_EQ (1, ledger.account_count ());
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (celerix::seconds_since_epoch (), info->modified);
	ASSERT_LT (celerix::seconds_since_epoch () - info->modified, 10);
	// Genesis block should be confirmed by default
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height.get (transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, celerix::dev::genesis->hash ());
}

TEST (ledger, process_modifies_sideband)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));
	ASSERT_EQ (send1->sideband ().timestamp, ledger.any.block_get (ledger.tx_begin_read (), send1->hash ())->sideband ().timestamp);
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (key2.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	celerix::block_hash hash1 = send->hash ();
	ASSERT_EQ (1, info1->block_count);
	// This was a valid block, it should progress.
	auto return1 = ledger.process (transaction, send);
	ASSERT_EQ (celerix::block_status::progress, return1);
	ASSERT_EQ (celerix::dev::genesis_key.pub, send->sideband ().account);
	ASSERT_EQ (2, send->sideband ().height);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.block_amount (transaction, hash1));
	ASSERT_EQ (celerix::dev::genesis_key.pub, send->account ());
	ASSERT_EQ (50, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.account_receivable (transaction, key2.pub));
	auto info2 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_EQ (2, info2->block_count);
	auto latest6 = ledger.any.block_get (transaction, info2->head);
	ASSERT_NE (nullptr, latest6);
	auto latest7 = dynamic_cast<celerix::send_block *> (latest6.get ());
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (*send, *latest7);
	// Create an open block opening an account accepting the send we just created
	auto open = builder
				.open ()
				.source (hash1)
				.representative (key2.pub)
				.account (key2.pub)
				.sign (key2.prv, key2.pub)
				.work (*pool.generate (key2.pub))
				.build ();
	celerix::block_hash hash2 (open->hash ());
	// This was a valid block, it should progress.
	auto return2 = ledger.process (transaction, open);
	ASSERT_EQ (celerix::block_status::progress, return2);
	ASSERT_EQ (key2.pub, open->sideband ().account);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, open->sideband ().balance.number ());
	ASSERT_EQ (1, open->sideband ().height);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.block_amount (transaction, hash2));
	ASSERT_EQ (celerix::block_status::progress, return2);
	ASSERT_EQ (key2.pub, open->account ());
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.block_amount (transaction, hash2));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_receivable (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (key2.pub));
	auto info3 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info3);
	auto latest2 = ledger.any.block_get (transaction, info3->head);
	ASSERT_NE (nullptr, latest2);
	auto latest3 = dynamic_cast<celerix::send_block *> (latest2.get ());
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (*send, *latest3);
	auto info4 = ledger.any.account_get (transaction, key2.pub);
	ASSERT_TRUE (info4);
	auto latest4 = ledger.any.block_get (transaction, info4->head);
	ASSERT_NE (nullptr, latest4);
	auto latest5 = dynamic_cast<celerix::open_block *> (latest4.get ());
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (*open, *latest5);
	ASSERT_FALSE (ledger.rollback (transaction, hash2));
	auto info5 = ledger.any.account_get (transaction, key2.pub);
	ASSERT_FALSE (info5);
	auto pending1 = ledger.any.pending_get (transaction, celerix::pending_key (key2.pub, hash1));
	ASSERT_TRUE (pending1);
	ASSERT_EQ (celerix::dev::genesis_key.pub, pending1->source);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, pending1->amount.number ());
	ASSERT_FALSE (ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.account_receivable (transaction, key2.pub));
	ASSERT_EQ (50, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (50, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	auto info6 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info6);
	ASSERT_EQ (hash1, info6->head);
	ASSERT_FALSE (ledger.rollback (transaction, info6->head));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	auto info7 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info7);
	ASSERT_EQ (1, info7->block_count);
	ASSERT_EQ (info1->head, info7->head);
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key (key2.pub, hash1)));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (0, ledger.account_receivable (transaction, key2.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, process_receive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (key2.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	celerix::block_hash hash1 (send->hash ());
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	celerix::keypair key3;
	auto open = builder
				.open ()
				.source (hash1)
				.representative (key3.pub)
				.account (key2.pub)
				.sign (key2.prv, key2.pub)
				.work (*pool.generate (key2.pub))
				.build ();
	celerix::block_hash hash2 (open->hash ());
	auto return1 = ledger.process (transaction, open);
	ASSERT_EQ (celerix::block_status::progress, return1);
	ASSERT_EQ (key2.pub, open->account ());
	ASSERT_EQ (key2.pub, open->sideband ().account);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, open->sideband ().balance.number ());
	ASSERT_EQ (1, open->sideband ().height);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.block_amount (transaction, hash2));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (key3.pub));
	auto send2 = builder
				 .send ()
				 .previous (hash1)
				 .destination (key2.pub)
				 .balance (25)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (hash1))
				 .build ();
	celerix::block_hash hash3 = send2->hash ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	auto receive = builder
				   .receive ()
				   .previous (hash2)
				   .source (hash3)
				   .sign (key2.prv, key2.pub)
				   .work (*pool.generate (hash2))
				   .build ();
	auto hash4 = receive->hash ();
	auto return2 = ledger.process (transaction, receive);
	ASSERT_EQ (key2.pub, receive->sideband ().account);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 25, receive->sideband ().balance.number ());
	ASSERT_EQ (2, receive->sideband ().height);
	ASSERT_EQ (25, ledger.any.block_amount (transaction, hash4).value ().number ());
	ASSERT_EQ (celerix::block_status::progress, return2);
	ASSERT_EQ (key2.pub, receive->account ());
	ASSERT_EQ (hash4, ledger.any.account_head (transaction, key2.pub));
	ASSERT_EQ (25, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (0, ledger.account_receivable (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 25, ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 25, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash4));
	ASSERT_FALSE (ledger.any.block_successor (transaction, hash2));
	ASSERT_EQ (25, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (25, ledger.account_receivable (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.any.account_head (transaction, key2.pub));
	auto pending1 = ledger.any.pending_get (transaction, celerix::pending_key (key2.pub, hash3));
	ASSERT_TRUE (pending1);
	ASSERT_EQ (celerix::dev::genesis_key.pub, pending1->source);
	ASSERT_EQ (25, pending1->amount.number ());
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, rollback_receiver)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (key2.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	celerix::block_hash hash1 (send->hash ());
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	celerix::keypair key3;
	auto open = builder
				.open ()
				.source (hash1)
				.representative (key3.pub)
				.account (key2.pub)
				.sign (key2.prv, key2.pub)
				.work (*pool.generate (key2.pub))
				.build ();
	celerix::block_hash hash2 (open->hash ());
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	ASSERT_EQ (hash2, ledger.any.account_head (transaction, key2.pub));
	ASSERT_EQ (50, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub).value ().number ());
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash1));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.any.account_balance (transaction, key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.any.account_get (transaction, key2.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ key2.pub, hash1 }));
}

TEST (ledger, rollback_representation)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key5;
	celerix::block_builder builder;
	auto change1 = builder
				   .change ()
				   .previous (celerix::dev::genesis->hash ())
				   .representative (key5.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (celerix::dev::genesis->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change1));
	celerix::keypair key3;
	auto change2 = builder
				   .change ()
				   .previous (change1->hash ())
				   .representative (key3.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (change1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change2));
	celerix::keypair key2;
	auto send1 = builder
				 .send ()
				 .previous (change2->hash ())
				 .destination (key2.pub)
				 .balance (50)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (change2->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	celerix::keypair key4;
	auto open = builder
				.open ()
				.source (send1->hash ())
				.representative (key4.pub)
				.account (key2.pub)
				.sign (key2.prv, key2.pub)
				.work (*pool.generate (key2.pub))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key2.pub)
				 .balance (1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	auto receive1 = builder
					.receive ()
					.previous (open->hash ())
					.source (send2->hash ())
					.sign (key2.prv, key2.pub)
					.work (*pool.generate (open->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 1, ledger.weight (key4.pub));
	auto info1 = ledger.any.account_get (transaction, key2.pub);
	ASSERT_TRUE (info1);
	ASSERT_EQ (key4.pub, info1->representative);
	ASSERT_FALSE (ledger.rollback (transaction, receive1->hash ()));
	auto info2 = ledger.any.account_get (transaction, key2.pub);
	ASSERT_TRUE (info2);
	ASSERT_EQ (key4.pub, info2->representative);
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (key4.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open->hash ()));
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (0, ledger.weight (key4.pub));
	ledger.rollback (transaction, send1->hash ());
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (key3.pub));
	auto info3 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info3);
	ASSERT_EQ (key3.pub, info3->representative);
	ASSERT_FALSE (ledger.rollback (transaction, change2->hash ()));
	auto info4 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info4);
	ASSERT_EQ (key5.pub, info4->representative);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (key5.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, receive_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	auto receive = builder
				   .receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (send->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive));
	ASSERT_FALSE (ledger.rollback (transaction, receive->hash ()));
}

TEST (ledger, process_duplicate)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (info1->head)
				.destination (key2.pub)
				.balance (50)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (info1->head))
				.build ();
	celerix::block_hash hash1 (send->hash ());
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	ASSERT_EQ (celerix::block_status::old, ledger.process (transaction, send));
	auto open = builder
				.open ()
				.source (hash1)
				.representative (1)
				.account (key2.pub)
				.sign (key2.prv, key2.pub)
				.work (*pool.generate (key2.pub))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	ASSERT_EQ (celerix::block_status::old, ledger.process (transaction, open));
}

TEST (ledger, representative_genesis)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto latest = ledger.any.account_head (transaction, celerix::dev::genesis_key.pub);
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (celerix::dev::genesis->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
}

TEST (ledger, representative_change)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	celerix::keypair key2;
	auto & pool = ctx.pool ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (info1->head)
				 .representative (key2.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (info1->head))
				 .build ();
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (celerix::block_status::progress, return1);
	ASSERT_EQ (0, ledger.any.block_amount (transaction, block->hash ()).value ().number ());
	ASSERT_EQ (celerix::dev::genesis_key.pub, block->account ());
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (key2.pub));
	auto info2 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_EQ (block->hash (), info2->head);
	ASSERT_FALSE (ledger.rollback (transaction, info2->head));
	auto info3 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info3);
	ASSERT_EQ (info1->head, info3->head);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key2;
	celerix::keypair key3;
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (info1->head)
				 .destination (key2.pub)
				 .balance (100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (info1->head))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block));
	auto block2 = builder
				  .send ()
				  .previous (info1->head)
				  .destination (key3.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (info1->head))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, block2));
}

TEST (ledger, receive_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key2;
	celerix::keypair key3;
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (info1->head)
				 .destination (key2.pub)
				 .balance (100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (info1->head))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block));
	auto block2 = builder
				  .open ()
				  .source (block->hash ())
				  .representative (key2.pub)
				  .account (key2.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (key2.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	auto block3 = builder
				  .change ()
				  .previous (block2->hash ())
				  .representative (key3.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block3));
	auto block4 = builder
				  .send ()
				  .previous (block->hash ())
				  .destination (key2.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block4));
	auto block5 = builder
				  .receive ()
				  .previous (block2->hash ())
				  .source (block4->hash ())
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, block5));
}

TEST (ledger, open_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key2;
	celerix::keypair key3;
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (info1->head)
				 .destination (key2.pub)
				 .balance (100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (info1->head))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block));
	auto block2 = builder
				  .open ()
				  .source (block->hash ())
				  .representative (key2.pub)
				  .account (key2.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (key2.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	auto block3 = builder
				  .open ()
				  .source (block->hash ())
				  .representative (key3.pub)
				  .account (key2.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (key2.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, block3));
}

TEST (ledger, representation_changes)
{
	auto store{ celerix::test::make_store () };
	celerix::keypair key1;
	celerix::rep_weights rep_weights{ store->rep_weight };
	ASSERT_EQ (0, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 1);
	ASSERT_EQ (1, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 2);
	ASSERT_EQ (2, rep_weights.representation_get (key1.pub));
}

TEST (ledger, delete_rep_weight_of_zero)
{
	auto store{ celerix::test::make_store () };
	celerix::rep_weights rep_weights{ store->rep_weight };
	auto txn{ store->tx_begin_write () };
	rep_weights.representation_add (txn, 1, 100);
	rep_weights.representation_add_dual (txn, 2, 100, 3, 100);
	ASSERT_EQ (3, rep_weights.size ());
	ASSERT_EQ (3, store->rep_weight.count (txn));

	// set rep weights to 0
	rep_weights.representation_add (txn, 1, celerix::uint128_t{ 0 } - 100);
	ASSERT_EQ (2, rep_weights.size ());
	ASSERT_EQ (2, store->rep_weight.count (txn));

	rep_weights.representation_add_dual (txn, 2, celerix::uint128_t{ 0 } - 100, 3, celerix::uint128_t{ 0 } - 100);
	ASSERT_EQ (0, rep_weights.size ());
	ASSERT_EQ (0, store->rep_weight.count (txn));
}

TEST (ledger, rep_cache_min_weight)
{
	auto store{ celerix::test::make_store () };
	celerix::uint128_t min_weight{ 10 };
	celerix::rep_weights rep_weights{ store->rep_weight, min_weight };
	auto txn{ store->tx_begin_write () };

	// one below min weight
	rep_weights.representation_add (txn, 1, 9);
	ASSERT_EQ (0, rep_weights.size ());
	ASSERT_EQ (1, store->rep_weight.count (txn));

	// exactly min weight
	rep_weights.representation_add (txn, 1, 1);
	ASSERT_EQ (1, rep_weights.size ());
	ASSERT_EQ (1, store->rep_weight.count (txn));

	// above min weight
	rep_weights.representation_add (txn, 1, 1);
	ASSERT_EQ (1, rep_weights.size ());
	ASSERT_EQ (1, store->rep_weight.count (txn));

	// fall blow min weight
	rep_weights.representation_add (txn, 1, celerix::uint128_t{ 0 } - 5);
	ASSERT_EQ (0, rep_weights.size ());
	ASSERT_EQ (1, store->rep_weight.count (txn));
}

TEST (ledger, representation)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto & rep_weights = ledger.cache.rep_weights;
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	celerix::keypair key2;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key2.pub)
				  .balance (celerix::dev::constants.genesis_amount - 100)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	celerix::keypair key3;
	auto block2 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (key3.pub)
				  .account (key2.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (key2.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	auto block3 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key2.pub)
				  .balance (celerix::dev::constants.genesis_amount - 200)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block3));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	auto block4 = builder
				  .receive ()
				  .previous (block2->hash ())
				  .source (block3->hash ())
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block4));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key3.pub));
	celerix::keypair key4;
	auto block5 = builder
				  .change ()
				  .previous (block4->hash ())
				  .representative (key4.pub)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block4->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block5));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key4.pub));
	celerix::keypair key5;
	auto block6 = builder
				  .send ()
				  .previous (block5->hash ())
				  .destination (key5.pub)
				  .balance (100)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block5->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block6));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	celerix::keypair key6;
	auto block7 = builder
				  .open ()
				  .source (block6->hash ())
				  .representative (key6.pub)
				  .account (key5.pub)
				  .sign (key5.prv, key5.pub)
				  .work (*pool.generate (key5.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block7));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	auto block8 = builder
				  .send ()
				  .previous (block6->hash ())
				  .destination (key5.pub)
				  .balance (0)
				  .sign (key2.prv, key2.pub)
				  .work (*pool.generate (block6->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block8));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	auto block9 = builder
				  .receive ()
				  .previous (block7->hash ())
				  .source (block8->hash ())
				  .sign (key5.prv, key5.pub)
				  .work (*pool.generate (block7->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block9));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 200, rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (key2.pub)
				 .account (key2.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*pool.generate (key2.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto open2 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (key2.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*pool.generate (key2.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, open2));
}

TEST (ledger, double_receive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key2;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (key2.pub)
				 .account (key2.pub)
				 .sign (key2.prv, key2.pub)
				 .work (*pool.generate (key2.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto receive1 = builder
					.receive ()
					.previous (open1->hash ())
					.source (send1->hash ())
					.sign (key2.prv, key2.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, receive1));
}

TEST (votes, check_signature)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = std::numeric_limits<celerix::uint128_t>::max ();
	auto & node1 = *system.add_node (node_config);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	{
		auto transaction = node1.ledger.tx_begin_write ();
		ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	}
	auto election1 = celerix::test::start_election (system, node1, send1->hash ());
	ASSERT_EQ (1, election1->votes ().size ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 1, 0);
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (celerix::vote_code::invalid, node1.vote_processor.vote_blocking (vote1, std::make_shared<celerix::transport::inproc::channel> (node1, node1)));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_processor.vote_blocking (vote1, std::make_shared<celerix::transport::inproc::channel> (node1, node1)));
	ASSERT_EQ (celerix::vote_code::replay, node1.vote_processor.vote_blocking (vote1, std::make_shared<celerix::transport::inproc::channel> (node1, node1)));
}

TEST (votes, add_one)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	node1.start_election (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()));
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_EQ (1, election1->votes ().size ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 1, 0);
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote1).at (send1->hash ()));
	auto vote2 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 2, 0);
	ASSERT_EQ (celerix::vote_code::ignored, node1.vote_router.vote (vote2).at (send1->hash ())); // Ignored due to vote cooldown
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes1 (election1->votes ());
	auto existing1 (votes1.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes1.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	celerix::lock_guard<celerix::mutex> guard{ node1.active.mutex };
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, winner.first);
}

namespace celerix
{
// Higher timestamps change the vote
TEST (votes, add_existing)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.online_weight_minimum = celerix::dev::constants.genesis_amount;
	node_config.backlog_scan.enable = false;
	auto & node1 = *system.add_node (node_config);
	celerix::keypair key1;
	celerix::block_builder builder;
	std::shared_ptr<celerix::block> send1 = builder.state ()
										 .account (celerix::dev::genesis_key.pub)
										 .previous (celerix::dev::genesis->hash ())
										 .representative (celerix::dev::genesis_key.pub) // No representative, blocks can't confirm
										 .balance (celerix::dev::constants.genesis_amount / 2 - celerix::Kcelerix_ratio)
										 .link (key1.pub)
										 .work (0)
										 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										 .build ();
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (node1.ledger.tx_begin_write (), send1));
	auto election1 = celerix::test::start_election (system, node1, send1->hash ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 1, 0);
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote1).at (send1->hash ()));
	// Block is already processed from vote
	ASSERT_TRUE (node1.active.publish (send1));
	ASSERT_EQ (celerix::vote::timestamp_min * 1, election1->last_votes[celerix::dev::genesis_key.pub].timestamp);
	celerix::keypair key2;
	std::shared_ptr<celerix::block> send2 = builder.state ()
										 .account (celerix::dev::genesis_key.pub)
										 .previous (celerix::dev::genesis->hash ())
										 .representative (celerix::dev::genesis_key.pub) // No representative, blocks can't confirm
										 .balance (celerix::dev::constants.genesis_amount / 2 - celerix::Kcelerix_ratio)
										 .link (key2.pub)
										 .work (0)
										 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										 .build ();
	node1.work_generate_blocking (*send2);
	ASSERT_FALSE (node1.active.publish (send2));
	ASSERT_TIMELY (5s, node1.active.active (*send2));
	auto vote2 = celerix::test::make_vote (celerix::dev::genesis_key, { send2 }, celerix::vote::timestamp_min * 2, 0);
	// Pretend we've waited the timeout
	auto vote_info1 = election1->get_last_vote (celerix::dev::genesis_key.pub);
	vote_info1.time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	election1->set_last_vote (celerix::dev::genesis_key.pub, vote_info1);
	ASSERT_EQ (celerix::vote_code::vote, node1.vote_router.vote (vote2).at (send2->hash ()));
	ASSERT_EQ (celerix::vote::timestamp_min * 2, election1->last_votes[celerix::dev::genesis_key.pub].timestamp);
	// Also resend the old vote, and see if we respect the timestamp
	auto vote_info2 = election1->get_last_vote (celerix::dev::genesis_key.pub);
	vote_info2.time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	election1->set_last_vote (celerix::dev::genesis_key.pub, vote_info2);
	ASSERT_EQ (celerix::vote_code::replay, node1.vote_router.vote (vote1).at (send1->hash ()));
	ASSERT_EQ (celerix::vote::timestamp_min * 2, election1->votes ()[celerix::dev::genesis_key.pub].timestamp);
	auto votes (election1->votes ());
	ASSERT_EQ (2, votes.size ());
	ASSERT_NE (votes.end (), votes.find (celerix::dev::genesis_key.pub));
	ASSERT_EQ (send2->hash (), votes[celerix::dev::genesis_key.pub].hash);
	ASSERT_EQ (*send2, *election1->tally ().begin ()->second);
}

// Lower timestamps are ignored
TEST (votes, add_old)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	node1.start_election (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()));
	auto election1 = node1.active.election (send1->qualified_root ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 2, 0);
	auto channel (std::make_shared<celerix::transport::inproc::channel> (node1, node1));
	node1.vote_processor.vote_blocking (vote1, channel);
	celerix::keypair key2;
	auto send2 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	auto vote2 = std::make_shared<celerix::vote> (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.prv, celerix::vote::timestamp_min * 1, 0, std::vector<celerix::block_hash>{ send2->hash () });
	auto vote_info = election1->get_last_vote (celerix::dev::genesis_key.pub);
	vote_info.time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	election1->set_last_vote (celerix::dev::genesis_key.pub, vote_info);
	node1.vote_processor.vote_blocking (vote2, channel);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (celerix::dev::genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes[celerix::dev::genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
}
}

// Lower timestamps are accepted for different accounts
// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/celerixcurrency/celerix-node/pull/3629
// Issue for investigating it: https://github.com/celerixcurrency/celerix-node/issues/3631
TEST (votes, DISABLED_add_old_different_account)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node1.process (send2));
	ASSERT_TRUE (celerix::test::start_elections (system, node1, { send1, send2 }));
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election2);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_EQ (1, election2->votes ().size ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 2, 0);
	auto channel (std::make_shared<celerix::transport::inproc::channel> (node1, node1));
	auto vote_result1 (node1.vote_processor.vote_blocking (vote1, channel));
	ASSERT_EQ (celerix::vote_code::vote, vote_result1);
	ASSERT_EQ (2, election1->votes ().size ());
	ASSERT_EQ (1, election2->votes ().size ());
	auto vote2 = celerix::test::make_vote (celerix::dev::genesis_key, { send2 }, celerix::vote::timestamp_min * 1, 0);
	auto vote_result2 (node1.vote_processor.vote_blocking (vote2, channel));
	ASSERT_EQ (celerix::vote_code::vote, vote_result2);
	ASSERT_EQ (2, election1->votes ().size ());
	ASSERT_EQ (2, election2->votes ().size ());
	auto votes1 (election1->votes ());
	auto votes2 (election2->votes ());
	ASSERT_NE (votes1.end (), votes1.find (celerix::dev::genesis_key.pub));
	ASSERT_NE (votes2.end (), votes2.find (celerix::dev::genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1[celerix::dev::genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), votes2[celerix::dev::genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
	ASSERT_EQ (*send2, *election2->winner ());
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	node1.start_election (send1);
	ASSERT_TIMELY (5s, node1.active.election (send1->qualified_root ()));
	auto election1 = node1.active.election (send1->qualified_root ());
	auto vote1 = celerix::test::make_vote (celerix::dev::genesis_key, { send1 }, celerix::vote::timestamp_min * 1, 0);
	auto channel (std::make_shared<celerix::transport::inproc::channel> (node1, node1));
	node1.vote_processor.vote_blocking (vote1, channel);
	celerix::keypair key2;
	auto send2 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key2.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	auto vote2 = celerix::test::make_vote (celerix::dev::genesis_key, { send2 }, celerix::vote::timestamp_min * 2, 0);
	node1.vote_processor.vote_blocking (vote2, channel);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (celerix::dev::genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes[celerix::dev::genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
}

// Query for block successor
TEST (ledger, successor)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (0)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	ASSERT_EQ (*send1, *node1.ledger.any.block_get (transaction, node1.ledger.any.block_successor (transaction, celerix::qualified_root (celerix::root (0), celerix::dev::genesis->hash ())).value ()));
	ASSERT_EQ (*celerix::dev::genesis, *node1.ledger.any.block_get (transaction, node1.ledger.any.block_successor (transaction, celerix::dev::genesis->qualified_root ()).value ()));
	ASSERT_FALSE (node1.ledger.any.block_successor (transaction, celerix::qualified_root (0)));
}

TEST (ledger, fail_change_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (celerix::dev::genesis->hash ())
				 .representative (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto result2 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::old, result2);
}

TEST (ledger, fail_change_gap_previous)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (1)
				 .representative (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::root (1)))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::gap_previous, result1);
}

TEST (ledger, fail_state_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto block = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (0)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::keypair ().prv, 0)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::bad_signature, result1);
}

TEST (ledger, fail_epoch_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto block = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount)
				 .link (ledger.epoch_link (celerix::epoch::epoch_1))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	block->signature.bytes[0] ^= 1;
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::bad_signature, result1); // Fails epoch signature
	block->signature.bytes[0] ^= 1;
	auto result2 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::progress, result2); // Succeeds with epoch signature
}

TEST (ledger, fail_change_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .change ()
				 .previous (celerix::dev::genesis->hash ())
				 .representative (key1.pub)
				 .sign (celerix::keypair ().prv, 0)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::bad_signature, result1);
}

TEST (ledger, fail_change_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .change ()
				  .previous (celerix::dev::genesis->hash ())
				  .representative (key1.pub)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	celerix::keypair key2;
	auto block2 = builder
				  .change ()
				  .previous (celerix::dev::genesis->hash ())
				  .representative (key2.pub)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::fork, result2);
}

TEST (ledger, fail_send_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto result2 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::old, result2);
}

TEST (ledger, fail_send_gap_previous)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (key1.pub)
				 .balance (1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::root (1)))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::gap_previous, result1);
}

TEST (ledger, fail_send_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (1)
				 .sign (celerix::keypair ().prv, 0)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto result1 = ledger.process (transaction, block);
	ASSERT_EQ (celerix::block_status::bad_signature, result1);
}

TEST (ledger, fail_send_negative_spend)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	celerix::keypair key2;
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key2.pub)
				  .balance (2)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::negative_spend, ledger.process (transaction, block2));
}

TEST (ledger, fail_send_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	celerix::keypair key2;
	auto block2 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key2.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	auto block2 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	ASSERT_EQ (celerix::block_status::old, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_gap_source)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block2 = builder
				  .open ()
				  .source (1)
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::gap_source, result2);
}

TEST (ledger, fail_open_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	auto block2 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	block2->signature.clear ();
	ASSERT_EQ (celerix::block_status::bad_signature, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_fork_previous)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block3));
	auto block4 = builder
				  .open ()
				  .source (block2->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, block4));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, fail_open_account_mismatch)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	celerix::keypair badkey;
	auto block2 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (badkey.pub)
				  .sign (badkey.prv, badkey.pub)
				  .work (*pool.generate (badkey.pub))
				  .build ();
	ASSERT_NE (celerix::block_status::progress, ledger.process (transaction, block2));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, fail_receive_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block3));
	auto block4 = builder
				  .receive ()
				  .previous (block3->hash ())
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block4));
	ASSERT_EQ (celerix::block_status::old, ledger.process (transaction, block4));
}

TEST (ledger, fail_receive_gap_source)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::progress, result3);
	auto block4 = builder
				  .receive ()
				  .previous (block3->hash ())
				  .source (1)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result4 = ledger.process (transaction, block4);
	ASSERT_EQ (celerix::block_status::gap_source, result4);
}

TEST (ledger, fail_receive_overreceive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result3);
	auto block3 = builder
				  .receive ()
				  .previous (block2->hash ())
				  .source (block1->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	auto result4 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::unreceivable, result4);
}

TEST (ledger, fail_receive_bad_signature)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::progress, result3);
	auto block4 = builder
				  .receive ()
				  .previous (block3->hash ())
				  .source (block2->hash ())
				  .sign (celerix::keypair ().prv, 0)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result4 = ledger.process (transaction, block4);
	ASSERT_EQ (celerix::block_status::bad_signature, result4);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::progress, result3);
	auto block4 = builder
				  .receive ()
				  .previous (1)
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (celerix::root (1)))
				  .build ();
	auto result4 = ledger.process (transaction, block4);
	ASSERT_EQ (celerix::block_status::gap_previous, result4);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block3 = builder
				  .receive ()
				  .previous (1)
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (celerix::root (1)))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::gap_previous, result3);
}

TEST (ledger, fail_receive_fork_previous)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::progress, result3);
	celerix::keypair key2;
	auto block4 = builder
				  .send ()
				  .previous (block3->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result4 = ledger.process (transaction, block4);
	ASSERT_EQ (celerix::block_status::progress, result4);
	auto block5 = builder
				  .receive ()
				  .previous (block3->hash ())
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result5 = ledger.process (transaction, block5);
	ASSERT_EQ (celerix::block_status::fork, result5);
}

TEST (ledger, fail_receive_received_source)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (key1.pub)
				  .balance (2)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto result1 = ledger.process (transaction, block1);
	ASSERT_EQ (celerix::block_status::progress, result1);
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	auto result2 = ledger.process (transaction, block2);
	ASSERT_EQ (celerix::block_status::progress, result2);
	auto block6 = builder
				  .send ()
				  .previous (block2->hash ())
				  .destination (key1.pub)
				  .balance (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	auto result6 = ledger.process (transaction, block6);
	ASSERT_EQ (celerix::block_status::progress, result6);
	auto block3 = builder
				  .open ()
				  .source (block1->hash ())
				  .representative (1)
				  .account (key1.pub)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (key1.pub))
				  .build ();
	auto result3 = ledger.process (transaction, block3);
	ASSERT_EQ (celerix::block_status::progress, result3);
	celerix::keypair key2;
	auto block4 = builder
				  .send ()
				  .previous (block3->hash ())
				  .destination (key1.pub)
				  .balance (1)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result4 = ledger.process (transaction, block4);
	ASSERT_EQ (celerix::block_status::progress, result4);
	auto block5 = builder
				  .receive ()
				  .previous (block4->hash ())
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block4->hash ()))
				  .build ();
	auto result5 = ledger.process (transaction, block5);
	ASSERT_EQ (celerix::block_status::progress, result5);
	auto block7 = builder
				  .receive ()
				  .previous (block3->hash ())
				  .source (block2->hash ())
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	auto result7 = ledger.process (transaction, block7);
	ASSERT_EQ (celerix::block_status::fork, result7);
}

TEST (ledger, latest_empty)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key;
	auto transaction = ledger.tx_begin_read ();
	auto latest = ledger.any.account_head (transaction, key.pub);
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub).as_account ());
	auto hash1 = ledger.any.account_head (transaction, celerix::dev::genesis_key.pub);
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (hash1)
				.destination (0)
				.balance (1)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (hash1))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	ASSERT_EQ (send->hash (), ledger.latest_root (transaction, celerix::dev::genesis_key.pub).as_block_hash ());
}

TEST (ledger, change_representative_move_representation)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key1;
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	celerix::block_builder builder;
	auto send = builder
				.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (key1.pub)
				.balance (0)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*pool.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	celerix::keypair key2;
	auto change = builder
				  .change ()
				  .previous (send->hash ())
				  .representative (key2.pub)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (send->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change));
	celerix::keypair key3;
	auto open = builder
				.open ()
				.source (send->hash ())
				.representative (key3.pub)
				.account (key1.pub)
				.sign (key1.prv, key1.pub)
				.work (*pool.generate (key1.pub))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info1);
	celerix::keypair key1;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (info1->head)
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 50)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (info1->head))
				 .build ();
	auto return1 = ledger.process (transaction, send1);
	ASSERT_EQ (celerix::block_status::progress, return1);
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto return2 = ledger.process (transaction, send2);
	ASSERT_EQ (celerix::block_status::progress, return2);
	celerix::keypair key2;
	auto open = builder
				.open ()
				.source (send2->hash ())
				.representative (key2.pub)
				.account (key1.pub)
				.sign (key1.prv, key1.pub)
				.work (*pool.generate (key1.pub))
				.build ();
	auto return4 = ledger.process (transaction, open);
	ASSERT_EQ (celerix::block_status::progress, return4);
	auto receive = builder
				   .receive ()
				   .previous (open->hash ())
				   .source (send1->hash ())
				   .sign (key1.prv, key1.pub)
				   .work (*pool.generate (open->hash ()))
				   .build ();
	auto return5 = ledger.process (transaction, receive);
	ASSERT_EQ (celerix::block_status::progress, return5);
	celerix::keypair key3;
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	auto change1 = builder
				   .change ()
				   .previous (send2->hash ())
				   .representative (key3.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (send2->hash ()))
				   .build ();
	auto return6 = ledger.process (transaction, change1);
	ASSERT_EQ (celerix::block_status::progress, return6);
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, receive->hash ()));
	ASSERT_EQ (50, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open->hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, change1->hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 100, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send2->hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 50, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send1->hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - 0, ledger.weight (celerix::dev::genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	celerix::keypair key2;
	auto & pool = ctx.pool ();
	{
		auto transaction = ledger.tx_begin_write ();
		auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
		ASSERT_TRUE (info1);
		celerix::block_builder builder;
		auto send = builder
					.send ()
					.previous (info1->head)
					.destination (key2.pub)
					.balance (std::numeric_limits<celerix::uint128_t>::max () - 50)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (info1->head))
					.build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	}
	ASSERT_EQ (2, ledger.block_count ());
	{
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (key2.pub));
	}
	{
		auto transaction = ledger.tx_begin_write ();
		auto info1 = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
		ASSERT_TRUE (info1);
		celerix::block_builder builder;
		auto send = builder
					.send ()
					.previous (info1->head)
					.destination (key2.pub)
					.balance (std::numeric_limits<celerix::uint128_t>::max () - 100)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (info1->head))
					.build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
	}
	ASSERT_EQ (3, ledger.block_count ());
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, block_destination_source)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair dest;
	celerix::uint128_t balance (celerix::dev::constants.genesis_amount);
	balance -= celerix::Kcelerix_ratio;
	celerix::block_builder builder;
	auto block1 = builder
				  .send ()
				  .previous (celerix::dev::genesis->hash ())
				  .destination (dest.pub)
				  .balance (balance)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	balance -= celerix::Kcelerix_ratio;
	auto block2 = builder
				  .send ()
				  .previous (block1->hash ())
				  .destination (celerix::dev::genesis_key.pub)
				  .balance (balance)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block1->hash ()))
				  .build ();
	balance += celerix::Kcelerix_ratio;
	auto block3 = builder
				  .receive ()
				  .previous (block2->hash ())
				  .source (block2->hash ())
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block2->hash ()))
				  .build ();
	balance -= celerix::Kcelerix_ratio;
	auto block4 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (block3->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (balance)
				  .link (dest.pub)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block3->hash ()))
				  .build ();
	balance -= celerix::Kcelerix_ratio;
	auto block5 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (block4->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (balance)
				  .link (celerix::dev::genesis_key.pub)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block4->hash ()))
				  .build ();
	balance += celerix::Kcelerix_ratio;
	auto block6 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (block5->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (balance)
				  .link (block5->hash ())
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (block5->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block1));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block2));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block3));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block4));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block5));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, block6));
	ASSERT_EQ (balance, ledger.any.block_balance (transaction, block6->hash ()));
	ASSERT_EQ (dest.pub, block1->destination ());
	ASSERT_FALSE (block1->source_field ());
	ASSERT_EQ (celerix::dev::genesis_key.pub, block2->destination ());
	ASSERT_FALSE (block2->source_field ());
	ASSERT_FALSE (block3->destination_field ());
	ASSERT_EQ (block2->hash (), block3->source ());
	ASSERT_EQ (dest.pub, block4->destination ());
	ASSERT_FALSE (block4->source_field ());
	ASSERT_EQ (celerix::dev::genesis_key.pub, block5->destination ());
	ASSERT_FALSE (block5->source_field ());
	ASSERT_FALSE (block6->destination_field ());
	ASSERT_EQ (block5->hash (), block6->source ());
}

TEST (ledger, state_account)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_EQ (celerix::dev::genesis_key.pub, ledger.any.block_account (transaction, send1->hash ()).value ());
}

TEST (ledger, state_send_receive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->is_send ());
	ASSERT_FALSE (send2->is_receive ());
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, receive1->hash ()));
	auto receive2 = ledger.any.block_get (transaction, receive1->hash ());
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (*receive1, *receive2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.block_balance (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->is_send ());
	ASSERT_TRUE (receive2->is_receive ());
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, receive1->hash ()));
	auto receive2 = ledger.any.block_get (transaction, receive1->hash ());
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (*receive1, *receive2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.block_balance (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->is_send ());
	ASSERT_TRUE (receive2->is_receive ());
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_rep_change)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair rep;
	celerix::block_builder builder;
	auto change1 = builder
				   .state ()
				   .account (celerix::dev::genesis_key.pub)
				   .previous (celerix::dev::genesis->hash ())
				   .representative (rep.pub)
				   .balance (celerix::dev::constants.genesis_amount)
				   .link (0)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (celerix::dev::genesis->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, change1->hash ()));
	auto change2 = ledger.any.block_get (transaction, change1->hash ());
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (*change1, *change2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.block_balance (transaction, change1->hash ()));
	ASSERT_EQ (0, ledger.any.block_amount (transaction, change1->hash ()).value ().number ());
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (2, change2->sideband ().height);
	ASSERT_FALSE (change2->is_send ());
	ASSERT_FALSE (change2->is_receive ());
	ASSERT_FALSE (change2->sideband ().details.is_epoch);
}

TEST (ledger, state_open)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ destination.pub, send1->hash () }));
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ destination.pub, send1->hash () }));
	ASSERT_TRUE (ledger.any.block_exists (transaction, open1->hash ()));
	auto open2 = ledger.any.block_get (transaction, open1->hash ());
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (*open1, *open2);
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, open1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, open1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (ledger.account_count (), store.account.count (transaction));
	ASSERT_EQ (1, open2->sideband ().height);
	ASSERT_FALSE (open2->is_send ());
	ASSERT_TRUE (open2->is_receive ());
	ASSERT_FALSE (open2->sideband ().details.is_epoch);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - (2 * celerix::Kcelerix_ratio))
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, send2));
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto receive1 = builder
					.receive ()
					.previous (send1->hash ())
					.source (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, receive1));
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	celerix::keypair rep;
	auto change1 = builder
				   .change ()
				   .previous (send1->hash ())
				   .representative (rep.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (send1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, change1));
}

TEST (ledger, state_unreceivable_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (1)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::gap_source, ledger.process (transaction, receive1));
}

TEST (ledger, state_receive_bad_amount_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::balance_mismatch, ledger.process (transaction, receive1));
}

TEST (ledger, state_no_link_amount_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	celerix::keypair rep;
	auto change1 = builder
				   .state ()
				   .account (celerix::dev::genesis_key.pub)
				   .previous (send1->hash ())
				   .representative (rep.pub)
				   .balance (celerix::dev::constants.genesis_amount)
				   .link (0)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (send1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::balance_mismatch, ledger.process (transaction, change1));
}

TEST (ledger, state_receive_wrong_account_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	celerix::keypair key;
	auto receive1 = builder
					.state ()
					.account (key.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (key.prv, key.pub)
					.work (*pool.generate (key.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, receive1));
}

TEST (ledger, state_open_state_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto open2 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, open2));
	ASSERT_EQ (open1->root (), open2->root ());
}

TEST (ledger, state_state_open_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto open2 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, open2));
	ASSERT_EQ (open1->root (), open2->root ());
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_open_previous_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (1)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (1))
				 .build ();
	ASSERT_EQ (celerix::block_status::gap_previous, ledger.process (transaction, open1));
}

TEST (ledger, state_open_source_fail)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (0)
				 .link (0)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::gap_source, ledger.process (transaction, open1));
}

TEST (ledger, state_send_change)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair rep;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (rep.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (rep.pub));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->is_send ());
	ASSERT_FALSE (send2->is_receive ());
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive_change)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	celerix::keypair rep;
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (rep.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, receive1->hash ()));
	auto receive2 = ledger.any.block_get (transaction, receive1->hash ());
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (*receive1, *receive2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.block_balance (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive1->hash ()));
	ASSERT_EQ (0, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->is_send ());
	ASSERT_TRUE (receive2->is_receive ());
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_open_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, open1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, open1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
}

TEST (ledger, state_receive_old)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - (2 * celerix::Kcelerix_ratio))
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto receive1 = builder
					.receive ()
					.previous (open1->hash ())
					.source (send2->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_EQ (2 * celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
}

TEST (ledger, state_rollback_send)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = ledger.any.block_get (transaction, send1->hash ());
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (*send1, *send2);
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	auto info = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info->amount.number ());
	ASSERT_FALSE (ledger.rollback (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_FALSE (ledger.any.block_successor (transaction, celerix::dev::genesis->hash ()));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_rollback_receive)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, receive1->hash () }));
	ASSERT_FALSE (ledger.rollback (transaction, receive1->hash ()));
	auto info = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info->amount.number ());
	ASSERT_FALSE (ledger.any.block_exists (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_rollback_received_send)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair key;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto receive1 = builder
					.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (key.prv, key.pub)
					.work (*pool.generate (key.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, receive1->hash () }));
	ASSERT_FALSE (ledger.rollback (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ledger.any.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (key.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_rep_change_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair rep;
	celerix::block_builder builder;
	auto change1 = builder
				   .state ()
				   .account (celerix::dev::genesis_key.pub)
				   .previous (celerix::dev::genesis->hash ())
				   .representative (rep.pub)
				   .balance (celerix::dev::constants.genesis_amount)
				   .link (0)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (celerix::dev::genesis->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change1));
	ASSERT_FALSE (ledger.rollback (transaction, change1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, change1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (rep.pub));
}

TEST (ledger, state_open_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_FALSE (ledger.rollback (transaction, open1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, open1->hash ()));
	ASSERT_FALSE (ledger.any.account_balance (transaction, destination.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	auto info = ledger.any.pending_get (transaction, celerix::pending_key (destination.pub, send1->hash ()));
	ASSERT_TRUE (info);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info->amount.number ());
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_send_change_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair rep;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (rep.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_FALSE (ledger.rollback (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, state_receive_change_rollback)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	celerix::keypair rep;
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send1->hash ())
					.representative (rep.pub)
					.balance (celerix::dev::constants.genesis_amount)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.rollback (transaction, receive1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, receive1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.any.account_balance (transaction, celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, epoch_blocks_v1_general)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto epoch1 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	ASSERT_FALSE (epoch1->is_send ());
	ASSERT_FALSE (epoch1->is_receive ());
	ASSERT_TRUE (epoch1->sideband ().details.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, epoch1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch1->sideband ().source_epoch); // Not used for epoch blocks
	auto epoch2 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (epoch1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (epoch1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, epoch2));
	auto genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1->hash ()));
	genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_0);
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_1);
	ASSERT_FALSE (epoch1->is_send ());
	ASSERT_FALSE (epoch1->is_receive ());
	ASSERT_TRUE (epoch1->sideband ().details.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, epoch1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch1->sideband ().source_epoch); // Not used for epoch blocks
	auto change1 = builder
				   .change ()
				   .previous (epoch1->hash ())
				   .representative (celerix::dev::genesis_key.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (epoch1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, change1));
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (epoch1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (send1->is_send ());
	ASSERT_FALSE (send1->is_receive ());
	ASSERT_FALSE (send1->sideband ().details.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, send1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, send1->sideband ().source_epoch); // Not used for send blocks
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, open1));
	auto epoch3 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (0)
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (destination.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::representative_mismatch, ledger.process (transaction, epoch3));
	auto epoch4 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (0)
				  .representative (0)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (destination.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch4));
	ASSERT_FALSE (epoch4->is_send ());
	ASSERT_FALSE (epoch4->is_receive ());
	ASSERT_TRUE (epoch4->sideband ().details.is_epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, epoch4->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch4->sideband ().source_epoch); // Not used for epoch blocks
	auto receive1 = builder
					.receive ()
					.previous (epoch4->hash ())
					.source (send1->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (epoch4->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, receive1));
	auto receive2 = builder
					.state ()
					.account (destination.pub)
					.previous (epoch4->hash ())
					.representative (destination.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (epoch4->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive2));
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().source_epoch);
	ASSERT_EQ (0, ledger.any.block_balance (transaction, epoch4->hash ()).value ().number ());
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, receive2->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive2->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.weight (destination.pub));
	ASSERT_FALSE (receive2->is_send ());
	ASSERT_TRUE (receive2->is_receive ());
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, epoch_blocks_v2_general)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto epoch1 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	// Trying to upgrade from epoch 0 to epoch 2. It is a requirement epoch upgrades are sequential unless the account is unopened
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, epoch1));
	// Set it to the first epoch and it should now succeed
	epoch1 = builder
			 .state ()
			 .account (celerix::dev::genesis_key.pub)
			 .previous (celerix::dev::genesis->hash ())
			 .representative (celerix::dev::genesis_key.pub)
			 .balance (celerix::dev::constants.genesis_amount)
			 .link (ledger.epoch_link (celerix::epoch::epoch_1))
			 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
			 .work (epoch1->work)
			 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	ASSERT_EQ (celerix::epoch::epoch_1, epoch1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch1->sideband ().source_epoch); // Not used for epoch blocks
	auto epoch2 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (epoch1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (epoch1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch2));
	ASSERT_EQ (celerix::epoch::epoch_2, epoch2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch2->sideband ().source_epoch); // Not used for epoch blocks
	auto epoch3 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (epoch2->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (epoch2->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, epoch3));
	auto genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_2);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1->hash ()));
	genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_0);
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	genesis_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (genesis_info);
	ASSERT_EQ (genesis_info->epoch (), celerix::epoch::epoch_1);
	auto change1 = builder
				   .change ()
				   .previous (epoch1->hash ())
				   .representative (celerix::dev::genesis_key.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (epoch1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, change1));
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (epoch1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_EQ (celerix::epoch::epoch_1, send1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, send1->sideband ().source_epoch); // Not used for send blocks
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, open1));
	auto epoch4 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (0)
				  .representative (0)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (destination.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch4));
	ASSERT_EQ (celerix::epoch::epoch_1, epoch4->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch4->sideband ().source_epoch); // Not used for epoch blocks
	auto epoch5 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (epoch4->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (epoch4->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::representative_mismatch, ledger.process (transaction, epoch5));
	auto epoch6 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (epoch4->hash ())
				  .representative (0)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (epoch4->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch6));
	ASSERT_EQ (celerix::epoch::epoch_2, epoch6->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch6->sideband ().source_epoch); // Not used for epoch blocks
	auto receive1 = builder
					.receive ()
					.previous (epoch6->hash ())
					.source (send1->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (epoch6->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::block_position, ledger.process (transaction, receive1));
	auto receive2 = builder
					.state ()
					.account (destination.pub)
					.previous (epoch6->hash ())
					.representative (destination.pub)
					.balance (celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (epoch6->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive2));
	ASSERT_EQ (celerix::epoch::epoch_2, receive2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().source_epoch);
	ASSERT_EQ (0, ledger.any.block_balance (transaction, epoch6->hash ()).value ().number ());
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_balance (transaction, receive2->hash ()));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.any.block_amount (transaction, receive2->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio, ledger.weight (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::Kcelerix_ratio, ledger.weight (destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto epoch1 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (epoch1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_EQ (celerix::epoch::epoch_1, send2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, send2->sideband ().source_epoch); // Not used for send blocks
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (destination.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (destination.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_EQ (celerix::epoch::epoch_0, open1->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, open1->sideband ().source_epoch);
	auto receive1 = builder
					.receive ()
					.previous (open1->hash ())
					.source (send2->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, receive1));
	auto receive2 = builder
					.state ()
					.account (destination.pub)
					.previous (open1->hash ())
					.representative (destination.pub)
					.balance (celerix::Kcelerix_ratio * 2)
					.link (send2->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive2));
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().source_epoch);
	auto destination_info = ledger.any.account_get (transaction, destination.pub);
	ASSERT_TRUE (destination_info);
	ASSERT_EQ (destination_info->epoch (), celerix::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, receive2->hash ()));
	destination_info = ledger.any.account_get (transaction, destination.pub);
	ASSERT_TRUE (destination_info);
	ASSERT_EQ (destination_info->epoch (), celerix::epoch::epoch_0);
	auto pending_send2 = ledger.any.pending_get (transaction, celerix::pending_key (destination.pub, send2->hash ()));
	ASSERT_TRUE (pending_send2);
	ASSERT_EQ (celerix::dev::genesis_key.pub, pending_send2->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, pending_send2->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_1, pending_send2->epoch);
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive2));
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_1, receive2->sideband ().source_epoch);
	destination_info = ledger.any.account_get (transaction, destination.pub);
	ASSERT_TRUE (destination_info);
	ASSERT_EQ (destination_info->epoch (), celerix::epoch::epoch_1);
	celerix::keypair destination2;
	auto send3 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (receive2->hash ())
				 .representative (destination.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (destination2.pub)
				 .sign (destination.prv, destination.pub)
				 .work (*pool.generate (receive2->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send3));
	auto open2 = builder
				 .open ()
				 .source (send3->hash ())
				 .representative (destination2.pub)
				 .account (destination2.pub)
				 .sign (destination2.prv, destination2.pub)
				 .work (*pool.generate (destination2.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, open2));
	// Upgrade to epoch 2 and send to destination. Try to create an open block from an epoch 2 source block.
	celerix::keypair destination3;
	auto epoch2 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (send2->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (send2->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch2));
	auto send4 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (epoch2->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 3)
				 .link (destination3.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (epoch2->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send4));
	auto open3 = builder
				 .open ()
				 .source (send4->hash ())
				 .representative (destination3.pub)
				 .account (destination3.pub)
				 .sign (destination3.prv, destination3.pub)
				 .work (*pool.generate (destination3.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::unreceivable, ledger.process (transaction, open3));
	// Send it to an epoch 1 account
	auto send5 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send4->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 4)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send4->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send5));
	destination_info = ledger.any.account_get (transaction, destination.pub);
	ASSERT_TRUE (destination_info);
	ASSERT_EQ (destination_info->epoch (), celerix::epoch::epoch_1);
	auto receive3 = builder
					.state ()
					.account (destination.pub)
					.previous (send3->hash ())
					.representative (destination.pub)
					.balance (celerix::Kcelerix_ratio * 2)
					.link (send5->hash ())
					.sign (destination.prv, destination.pub)
					.work (*pool.generate (send3->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive3));
	ASSERT_EQ (celerix::epoch::epoch_2, receive3->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_2, receive3->sideband ().source_epoch);
	destination_info = ledger.any.account_get (transaction, destination.pub);
	ASSERT_TRUE (destination_info);
	ASSERT_EQ (destination_info->epoch (), celerix::epoch::epoch_2);
	// Upgrade an unopened account straight to epoch 2
	celerix::keypair destination4;
	auto send6 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send5->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 5)
				 .link (destination4.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send5->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send6));
	auto epoch4 = builder
				  .state ()
				  .account (destination4.pub)
				  .previous (0)
				  .representative (0)
				  .balance (0)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (destination4.pub))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch4));
	ASSERT_EQ (celerix::epoch::epoch_2, epoch4->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch4->sideband ().source_epoch); // Not used for epoch blocks
	ASSERT_EQ (store.account.count (transaction), ledger.account_count ());
}

TEST (ledger, epoch_blocks_fork)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::account{})
				 .balance (celerix::dev::constants.genesis_amount)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto epoch1 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, epoch1));
	auto epoch2 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, epoch2));
	auto epoch3 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch3));
	ASSERT_EQ (celerix::epoch::epoch_1, epoch3->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch3->sideband ().source_epoch); // Not used for epoch state blocks
	auto epoch4 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_2))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::fork, ledger.process (transaction, epoch2));
}

TEST (ledger, successor_epoch)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto open = builder
				.state ()
				.account (key1.pub)
				.previous (0)
				.representative (key1.pub)
				.balance (1)
				.link (send1->hash ())
				.sign (key1.prv, key1.pub)
				.work (*pool.generate (key1.pub))
				.build ();
	auto change = builder
				  .state ()
				  .account (key1.pub)
				  .previous (open->hash ())
				  .representative (key1.pub)
				  .balance (1)
				  .link (0)
				  .sign (key1.prv, key1.pub)
				  .work (*pool.generate (open->hash ()))
				  .build ();
	auto open_hash = open->hash ();
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (reinterpret_cast<celerix::account const &> (open_hash))
				 .balance (celerix::dev::constants.genesis_amount - 2)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto epoch_open = builder
					  .state ()
					  .account (reinterpret_cast<celerix::account const &> (open_hash))
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*pool.generate (open->hash ()))
					  .build ();
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send1));
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, open));
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, change));
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, send2));
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, epoch_open));
	ASSERT_EQ (*change, *node1.ledger.any.block_get (transaction, node1.ledger.any.block_successor (transaction, change->qualified_root ()).value ()));
	ASSERT_EQ (*epoch_open, *node1.ledger.any.block_get (transaction, node1.ledger.any.block_successor (transaction, epoch_open->qualified_root ()).value ()));
	ASSERT_EQ (celerix::epoch::epoch_1, epoch_open->sideband ().details.epoch);
	ASSERT_EQ (celerix::epoch::epoch_0, epoch_open->sideband ().source_epoch); // Not used for epoch state blocks
}

TEST (ledger, epoch_open_pending)
{
	celerix::block_builder builder{};
	celerix::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::keypair key1{};
	auto epoch_open = builder.state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
					  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();
	auto process_result = node1.ledger.process (node1.ledger.tx_begin_write (), epoch_open);
	ASSERT_EQ (celerix::block_status::gap_epoch_open_pending, process_result);
	node1.block_processor.add (epoch_open);
	// Waits for the block to get saved in the database
	ASSERT_TIMELY_EQ (10s, 1, node1.unchecked.count ());
	ASSERT_FALSE (node1.block_or_pruned_exists (epoch_open->hash ()));
	// Open block should be inserted into unchecked
	auto blocks = node1.unchecked.get (celerix::hash_or_account (epoch_open->account_field ().value ()).hash);
	ASSERT_EQ (blocks.size (), 1);
	ASSERT_EQ (blocks[0].block->full_hash (), epoch_open->full_hash ());
	// New block to process epoch open
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	node1.block_processor.add (send1);
	ASSERT_TIMELY (10s, node1.block_or_pruned_exists (epoch_open->hash ()));
}

TEST (ledger, block_hash_account_conflict)
{
	celerix::block_builder builder;
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair key1;
	celerix::keypair key2;
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	/*
	 * Generate a send block whose destination is a block hash already
	 * in the ledger and not an account
	 */
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();

	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build ();

	/*
	 * Note that the below link is a block hash when this is intended
	 * to represent a send state block. This can generally never be
	 * received , except by epoch blocks, which can sign an open block
	 * for arbitrary accounts.
	 */
	auto send2 = builder.state ()
				 .account (key1.pub)
				 .previous (receive1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (90)
				 .link (receive1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (receive1->hash ()))
				 .build ();

	/*
	 * Generate an epoch open for the account with the same value as the block hash
	 */
	auto receive1_hash = receive1->hash ();
	auto open_epoch1 = builder.state ()
					   .account (reinterpret_cast<celerix::account const &> (receive1_hash))
					   .previous (0)
					   .representative (0)
					   .balance (0)
					   .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*pool.generate (receive1->hash ()))
					   .build ();

	node1.work_generate_blocking (*send1);
	node1.work_generate_blocking (*receive1);
	node1.work_generate_blocking (*send2);
	node1.work_generate_blocking (*open_epoch1);
	ASSERT_EQ (celerix::block_status::progress, node1.process (send1));
	ASSERT_EQ (celerix::block_status::progress, node1.process (receive1));
	ASSERT_EQ (celerix::block_status::progress, node1.process (send2));
	ASSERT_EQ (celerix::block_status::progress, node1.process (open_epoch1));
	ASSERT_TRUE (celerix::test::start_elections (system, node1, { send1, receive1, send2, open_epoch1 }));
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (receive1->qualified_root ());
	ASSERT_NE (nullptr, election2);
	auto election3 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election3);
	auto election4 = node1.active.election (open_epoch1->qualified_root ());
	ASSERT_NE (nullptr, election4);
	auto winner1 (election1->winner ());
	auto winner2 (election2->winner ());
	auto winner3 (election3->winner ());
	auto winner4 (election4->winner ());
	ASSERT_EQ (*send1, *winner1);
	ASSERT_EQ (*receive1, *winner2);
	ASSERT_EQ (*send2, *winner3);
	ASSERT_EQ (*open_epoch1, *winner4);
}

TEST (ledger, unchecked_epoch)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (destination.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*open1);
	auto epoch1 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (open1->hash ())
				  .representative (destination.pub)
				  .balance (celerix::Kcelerix_ratio)
				  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (0)
				  .build ();
	node1.work_generate_blocking (*epoch1);
	node1.block_processor.add (epoch1);
	{
		// Waits for the epoch1 block to pass through block_processor and unchecked.put queues
		ASSERT_TIMELY_EQ (10s, 1, node1.unchecked.count ());
		auto blocks = node1.unchecked.get (epoch1->previous ());
		ASSERT_EQ (blocks.size (), 1);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	ASSERT_TIMELY (5s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), epoch1->hash ()));
	{
		// Waits for the last blocks to pass through block_processor and unchecked.put queues
		ASSERT_TIMELY_EQ (10s, 0, node1.unchecked.count ());
		auto info = node1.ledger.any.account_get (node1.ledger.tx_begin_read (), destination.pub);
		ASSERT_TRUE (info);
		ASSERT_EQ (info->epoch (), celerix::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_epoch_invalid)
{
	celerix::test::system system;
	celerix::node_config node_config = system.default_config ();
	node_config.backlog_scan.enable = false;
	auto & node1 (*system.add_node (node_config));
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto open1 = builder
				 .state ()
				 .account (destination.pub)
				 .previous (0)
				 .representative (destination.pub)
				 .balance (celerix::Kcelerix_ratio)
				 .link (send1->hash ())
				 .sign (destination.prv, destination.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*open1);
	// Epoch block with account own signature
	auto epoch1 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (open1->hash ())
				  .representative (destination.pub)
				  .balance (celerix::Kcelerix_ratio)
				  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (destination.prv, destination.pub)
				  .work (0)
				  .build ();
	node1.work_generate_blocking (*epoch1);
	// Pseudo epoch block (send subtype, destination - epoch link)
	auto epoch2 = builder
				  .state ()
				  .account (destination.pub)
				  .previous (open1->hash ())
				  .representative (destination.pub)
				  .balance (celerix::Kcelerix_ratio - 1)
				  .link (node1.ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (destination.prv, destination.pub)
				  .work (0)
				  .build ();
	node1.work_generate_blocking (*epoch2);
	node1.block_processor.add (epoch1);
	node1.block_processor.add (epoch2);
	{
		// Waits for the last blocks to pass through block_processor and unchecked.put queues
		ASSERT_TIMELY_EQ (10s, 2, node1.unchecked.count ());
		auto blocks = node1.unchecked.get (epoch1->previous ());
		ASSERT_EQ (blocks.size (), 2);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	// Waits for the last blocks to pass through block_processor and unchecked.put queues
	ASSERT_TIMELY (10s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), epoch2->hash ()));
	{
		auto transaction = node1.ledger.tx_begin_read ();
		ASSERT_FALSE (node1.ledger.any.block_exists (transaction, epoch1->hash ()));
		auto unchecked_count = node1.unchecked.count ();
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.unchecked.count ());
		auto info = node1.ledger.any.account_get (transaction, destination.pub);
		ASSERT_TRUE (info);
		ASSERT_NE (info->epoch (), celerix::epoch::epoch_1);
		auto epoch2_store = node1.ledger.any.block_get (transaction, epoch2->hash ());
		ASSERT_NE (nullptr, epoch2_store);
		ASSERT_EQ (celerix::epoch::epoch_0, epoch2_store->sideband ().details.epoch);
		ASSERT_TRUE (epoch2_store->is_send ());
		ASSERT_FALSE (epoch2_store->sideband ().details.is_epoch);
		ASSERT_FALSE (epoch2_store->is_receive ());
	}
}

TEST (ledger, unchecked_open)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::keypair destination;
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (destination.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*open1);
	// Invalid signature for open block
	auto open2 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*open2);
	open2->signature.bytes[0] ^= 1;
	node1.block_processor.add (open2); // Insert open2 in to the queue before open1
	node1.block_processor.add (open1);
	{
		// Waits for the last blocks to pass through block_processor and unchecked.put queues
		ASSERT_TIMELY_EQ (10s, 1, node1.unchecked.count ());
		// Get the next peer for attempting a tcp bootstrap connection
		auto blocks = node1.unchecked.get (open1->source_field ().value ());
		ASSERT_EQ (blocks.size (), 1);
	}
	node1.block_processor.add (send1);
	// Waits for the send1 block to pass through block_processor and unchecked.put queues
	ASSERT_TIMELY (5s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), open1->hash ()));
	ASSERT_EQ (0, node1.unchecked.count ());
}

TEST (ledger, unchecked_receive)
{
	celerix::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	celerix::keypair destination{};
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send1);
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (destination.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*send2);
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (destination.pub)
				 .account (destination.pub)
				 .sign (destination.prv, destination.pub)
				 .work (0)
				 .build ();
	node1.work_generate_blocking (*open1);
	auto receive1 = builder
					.receive ()
					.previous (open1->hash ())
					.source (send2->hash ())
					.sign (destination.prv, destination.pub)
					.work (0)
					.build ();
	node1.work_generate_blocking (*receive1);
	node1.block_processor.add (send1);
	node1.block_processor.add (receive1);
	auto check_block_is_listed = [&] (celerix::store::transaction const & transaction_a, celerix::block_hash const & block_hash_a) {
		return !node1.unchecked.get (block_hash_a).empty ();
	};
	// Previous block for receive1 is unknown, signature cannot be validated
	{
		// Waits for the last blocks to pass through block_processor and unchecked.put queues
		ASSERT_TIMELY (15s, check_block_is_listed (node1.store.tx_begin_read (), receive1->previous ()));
		auto blocks = node1.unchecked.get (receive1->previous ());
		ASSERT_EQ (blocks.size (), 1);
	}
	// Waits for the open1 block to pass through block_processor and unchecked.put queues
	node1.block_processor.add (open1);
	ASSERT_TIMELY (15s, check_block_is_listed (node1.store.tx_begin_read (), receive1->source_field ().value ()));
	// Previous block for receive1 is known, signature was validated
	{
		auto transaction = node1.store.tx_begin_read ();
		auto blocks (node1.unchecked.get (receive1->source_field ().value ()));
		ASSERT_EQ (blocks.size (), 1);
	}
	node1.block_processor.add (send2);
	ASSERT_TIMELY (10s, node1.ledger.any.block_exists (node1.ledger.tx_begin_read (), receive1->hash ()));
	ASSERT_EQ (0, node1.unchecked.count ());
}

TEST (ledger, confirmation_height_not_updated)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	auto & pool = ctx.pool ();
	auto account_info = ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (account_info);
	celerix::keypair key;
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (account_info->head)
				 .destination (key.pub)
				 .balance (50)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (account_info->head))
				 .build ();
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height.get (transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (celerix::dev::genesis->hash (), confirmation_height_info.frontier);
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_FALSE (store.confirmation_height.get (transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (celerix::dev::genesis->hash (), confirmation_height_info.frontier);
	auto open1 = builder
				 .open ()
				 .source (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (key.pub)
				 .sign (key.prv, key.pub)
				 .work (*pool.generate (key.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_TRUE (store.confirmation_height.get (transaction, key.pub, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (celerix::block_hash (0), confirmation_height_info.frontier);
}

TEST (ledger, zero_rep)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_builder builder;
	auto block1 = builder.state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (0)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (celerix::dev::genesis->hash ()))
				  .build ();
	auto transaction = node1.ledger.tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, block1));
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node1.ledger.cache.rep_weights.representation_get (0));
	auto block2 = builder.state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (block1->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (0)
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*system.work.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, node1.ledger.process (transaction, block2));
	ASSERT_EQ (celerix::dev::constants.genesis_amount, node1.ledger.cache.rep_weights.representation_get (celerix::dev::genesis_key.pub));
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (0));
}

TEST (ledger, work_validation)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;
	auto gen = celerix::dev::genesis_key;
	celerix::keypair key;

	// With random work the block doesn't pass, then modifies the block with sufficient work and ensures a correct result
	auto process_block = [&store, &ledger, &pool] (std::shared_ptr<celerix::block> block_a, celerix::block_details const details_a) {
		auto threshold = celerix::dev::network_params.work.threshold (block_a->work_version (), details_a);
		// Rarely failed with random work, so modify until it doesn't have enough difficulty
		while (celerix::dev::network_params.work.difficulty (*block_a) >= threshold)
		{
			block_a->block_work_set (block_a->block_work () + 1);
		}
		EXPECT_EQ (celerix::block_status::insufficient_work, ledger.process (ledger.tx_begin_write (), block_a));
		block_a->block_work_set (*pool.generate (block_a->root (), threshold));
		EXPECT_EQ (celerix::block_status::progress, ledger.process (ledger.tx_begin_write (), block_a));
	};

	std::error_code ec;

	auto send = builder.send ()
				.previous (celerix::dev::genesis->hash ())
				.destination (gen.pub)
				.balance (celerix::dev::constants.genesis_amount - 1)
				.sign (gen.prv, gen.pub)
				.work (0)
				.build (ec);
	ASSERT_FALSE (ec);

	auto receive = builder.receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (gen.prv, gen.pub)
				   .work (0)
				   .build (ec);
	ASSERT_FALSE (ec);

	auto change = builder.change ()
				  .previous (receive->hash ())
				  .representative (key.pub)
				  .sign (gen.prv, gen.pub)
				  .work (0)
				  .build (ec);
	ASSERT_FALSE (ec);

	auto state = builder.state ()
				 .account (gen.pub)
				 .previous (change->hash ())
				 .representative (gen.pub)
				 .balance (celerix::dev::constants.genesis_amount - 1)
				 .link (key.pub)
				 .sign (gen.prv, gen.pub)
				 .work (0)
				 .build (ec);
	ASSERT_FALSE (ec);

	auto open = builder.open ()
				.account (key.pub)
				.source (state->hash ())
				.representative (key.pub)
				.sign (key.prv, key.pub)
				.work (0)
				.build (ec);
	ASSERT_FALSE (ec);

	auto epoch = builder.state ()
				 .account (key.pub)
				 .previous (open->hash ())
				 .balance (1)
				 .representative (key.pub)
				 .link (ledger.epoch_link (celerix::epoch::epoch_1))
				 .sign (gen.prv, gen.pub)
				 .work (0)
				 .build (ec);
	ASSERT_FALSE (ec);

	process_block (send, {});
	process_block (receive, {});
	process_block (change, {});
	process_block (state, celerix::block_details (celerix::epoch::epoch_0, true, false, false));
	process_block (open, {});
	process_block (epoch, celerix::block_details (celerix::epoch::epoch_1, false, false, true));
}

TEST (ledger, dependents_confirmed)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	celerix::block_builder builder;
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *celerix::dev::genesis));
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *send1));
	auto send2 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *send2));
	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive1));
	ledger.confirm (transaction, send1->hash ());
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive1));
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (receive1->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (receive1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive2));
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive2));
	ledger.confirm (transaction, receive1->hash ());
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive2));
	ledger.confirm (transaction, send2->hash ());
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive2));
}

TEST (ledger, dependents_confirmed_pruning)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_FALSE (store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::block_builder builder;
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::keypair key1;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	auto send2 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ledger.confirm (transaction, send2->hash ());
	ASSERT_TRUE (ledger.confirmed.block_exists_or_pruned (transaction, send1->hash ()));
	ASSERT_EQ (2, ledger.pruning_action (transaction, send2->hash (), 1));
	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (celerix::dev::genesis_key.pub)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build ();
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive1));
}

TEST (ledger, block_confirmed)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto transaction = ledger.tx_begin_write ();
	celerix::block_builder builder;
	ASSERT_TRUE (ledger.confirmed.block_exists_or_pruned (transaction, celerix::dev::genesis->hash ()));
	auto & pool = ctx.pool ();
	celerix::keypair key1;
	auto send1 = builder.state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	// Must be safe against non-existing blocks
	ASSERT_FALSE (ledger.confirmed.block_exists_or_pruned (transaction, send1->hash ()));
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_FALSE (ledger.confirmed.block_exists_or_pruned (transaction, send1->hash ()));
	ledger.confirm (transaction, send1->hash ());
	ASSERT_TRUE (ledger.confirmed.block_exists_or_pruned (transaction, send1->hash ()));
}

TEST (ledger, cache)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto & stats = ctx.stats ();
	auto & pool = ctx.pool ();
	celerix::block_builder builder;

	size_t const total = 100;

	// Check existing ledger (incremental cache update) and reload on a new ledger
	for (size_t i (0); i < total; ++i)
	{
		auto account_count = 1 + i;
		auto block_count = 1 + 2 * (i + 1) - 2;
		auto cemented_count = 1 + 2 * (i + 1) - 2;
		auto genesis_weight = celerix::dev::constants.genesis_amount - i;
		auto pruned_count = i;

		auto cache_check = [&, i] (celerix::ledger const & ledger) {
			ASSERT_EQ (account_count, ledger.account_count ());
			ASSERT_EQ (block_count, ledger.block_count ());
			ASSERT_EQ (cemented_count, ledger.cemented_count ());
			ASSERT_EQ (genesis_weight, ledger.cache.rep_weights.representation_get (celerix::dev::genesis_key.pub));
			ASSERT_EQ (pruned_count, ledger.pruned_count ());
		};

		celerix::keypair key;
		auto const latest = ledger.any.account_head (ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
		auto send = builder.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (latest)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - (i + 1))
					.link (key.pub)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (latest))
					.build ();
		auto open = builder.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (1)
					.link (send->hash ())
					.sign (key.prv, key.pub)
					.work (*pool.generate (key.pub))
					.build ();
		{
			auto transaction = ledger.tx_begin_write ();
			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
		}

		++block_count;
		--genesis_weight;
		cache_check (ledger);
		cache_check (celerix::ledger (store, stats, celerix::dev::constants));

		{
			auto transaction = ledger.tx_begin_write ();
			ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open));
		}

		++block_count;
		++account_count;
		cache_check (ledger);
		cache_check (celerix::ledger (store, stats, celerix::dev::constants));

		{
			auto transaction = ledger.tx_begin_write ();
			ledger.confirm (transaction, send->hash ());
			ASSERT_TRUE (ledger.confirmed.block_exists_or_pruned (transaction, send->hash ()));
		}

		++cemented_count;
		cache_check (ledger);
		cache_check (celerix::ledger (store, stats, celerix::dev::constants));

		{
			auto transaction = ledger.tx_begin_write ();
			ledger.confirm (transaction, open->hash ());
			ASSERT_TRUE (ledger.confirmed.block_exists_or_pruned (transaction, open->hash ()));
		}

		++cemented_count;
		cache_check (ledger);
		cache_check (celerix::ledger (store, stats, celerix::dev::constants));

		{
			auto transaction = ledger.tx_begin_write ();
			ledger.pruning_action (transaction, open->hash (), 1);
		}
		++pruned_count;
		cache_check (ledger);
		cache_check (celerix::ledger (store, stats, celerix::dev::constants));
	}
}

TEST (ledger, pruning_action)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send1_stored (store->block.get (transaction, send1->hash ()));
	ASSERT_NE (nullptr, send1_stored);
	ASSERT_EQ (*send1, *send1_stored);
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	// Pruning action
	ledger.confirm (transaction, send1->hash ());
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1->hash (), 1));
	ASSERT_EQ (0, ledger.pruning_action (transaction, celerix::dev::genesis->hash (), 1));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists_or_pruned (transaction, send1->hash ()));
	// Pruned ledger start without proper flags emulation
	ledger.pruning = false;
	ASSERT_TRUE (ledger.any.block_exists_or_pruned (transaction, send1->hash ()));
	ledger.pruning = true;
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	// Receiving pruned block
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send2->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send2->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, receive1->hash ()));
	auto receive1_stored (store->block.get (transaction, receive1->hash ()));
	ASSERT_NE (nullptr, receive1_stored);
	ASSERT_EQ (*receive1, *receive1_stored);
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (4, receive1_stored->sideband ().height);
	ASSERT_FALSE (receive1_stored->is_send ());
	ASSERT_TRUE (receive1_stored->is_receive ());
	ASSERT_FALSE (receive1_stored->sideband ().details.is_epoch);
	// Middle block pruning
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ledger.confirm (transaction, send2->hash ());
	ASSERT_EQ (1, ledger.pruning_action (transaction, send2->hash (), 1));
	ASSERT_TRUE (store->pruned.exists (transaction, send2->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send2->hash ()));
	ASSERT_EQ (store->account.count (transaction), ledger.account_count ());
	ASSERT_EQ (store->pruned.count (transaction), ledger.pruned_count ());
	ASSERT_EQ (store->block.count (transaction), ledger.block_count () - ledger.pruned_count ());
}

TEST (ledger, pruning_large_chain)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	size_t send_receive_pairs (20);
	auto last_hash (celerix::dev::genesis->hash ());
	celerix::block_builder builder;
	for (auto i (0); i < send_receive_pairs; i++)
	{
		auto send = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (last_hash)
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
					.link (celerix::dev::genesis_key.pub)
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (last_hash))
					.build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send));
		ASSERT_TRUE (ledger.any.block_exists (transaction, send->hash ()));
		auto receive = builder
					   .state ()
					   .account (celerix::dev::genesis_key.pub)
					   .previous (send->hash ())
					   .representative (celerix::dev::genesis_key.pub)
					   .balance (celerix::dev::constants.genesis_amount)
					   .link (send->hash ())
					   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					   .work (*pool.generate (send->hash ()))
					   .build ();
		ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive));
		ASSERT_TRUE (ledger.any.block_exists (transaction, receive->hash ()));
		last_hash = receive->hash ();
	}
	ASSERT_EQ (0, store->pruned.count (transaction));
	ASSERT_EQ (send_receive_pairs * 2 + 1, store->block.count (transaction));
	ledger.confirm (transaction, last_hash);
	// Pruning action
	ASSERT_EQ (send_receive_pairs * 2, ledger.pruning_action (transaction, last_hash, 5));
	ASSERT_TRUE (store->pruned.exists (transaction, last_hash));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, last_hash));
	ASSERT_EQ (store->pruned.count (transaction), ledger.pruned_count ());
	ASSERT_EQ (store->block.count (transaction), ledger.block_count () - ledger.pruned_count ());
	ASSERT_EQ (send_receive_pairs * 2, store->pruned.count (transaction));
	ASSERT_EQ (1, store->block.count (transaction)); // Genesis
}

TEST (ledger, pruning_source_rollback)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto epoch1 = builder
				  .state ()
				  .account (celerix::dev::genesis_key.pub)
				  .previous (celerix::dev::genesis->hash ())
				  .representative (celerix::dev::genesis_key.pub)
				  .balance (celerix::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (celerix::epoch::epoch_1))
				  .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				  .work (*pool.generate (celerix::dev::genesis->hash ()))
				  .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, epoch1));
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (epoch1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (epoch1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ledger.confirm (transaction, send1->hash ());
	// Pruning action
	ASSERT_EQ (2, ledger.pruning_action (transaction, send1->hash (), 1));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, epoch1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, epoch1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	auto info = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_1, info->epoch);
	// Receiving pruned block
	auto receive1 = builder
					.state ()
					.account (celerix::dev::genesis_key.pub)
					.previous (send2->hash ())
					.representative (celerix::dev::genesis_key.pub)
					.balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
					.link (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send2->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (5, ledger.block_count ());
	// Rollback receive block
	ASSERT_FALSE (ledger.rollback (transaction, receive1->hash ()));
	auto info2 = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info2);
	ASSERT_NE (celerix::dev::genesis_key.pub, info2->source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (celerix::Kcelerix_ratio, info2->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_1, info2->epoch);
	// Process receive block again
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (5, ledger.block_count ());
}

TEST (ledger, pruning_source_rollback_legacy)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	celerix::keypair key1;
	auto send2 = builder
				 .send ()
				 .previous (send1->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ key1.pub, send2->hash () }));
	auto send3 = builder
				 .send ()
				 .previous (send2->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 3 * celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send2->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send3));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send3->hash ()));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send3->hash () }));
	ledger.confirm (transaction, send2->hash ());
	// Pruning action
	ASSERT_EQ (2, ledger.pruning_action (transaction, send2->hash (), 1));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, send2->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	auto info1 = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info1);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info1->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info1->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_0, info1->epoch);
	auto info2 = ledger.any.pending_get (transaction, celerix::pending_key (key1.pub, send2->hash ()));
	ASSERT_TRUE (info2);
	ASSERT_EQ (celerix::dev::genesis_key.pub, info2->source);
	ASSERT_EQ (celerix::Kcelerix_ratio, info2->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_0, info2->epoch);
	// Receiving pruned block
	auto receive1 = builder
					.receive ()
					.previous (send3->hash ())
					.source (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send3->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (5, ledger.block_count ());
	// Rollback receive block
	ASSERT_FALSE (ledger.rollback (transaction, receive1->hash ()));
	auto info3 = ledger.any.pending_get (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send1->hash ()));
	ASSERT_TRUE (info3);
	ASSERT_NE (celerix::dev::genesis_key.pub, info3->source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (celerix::Kcelerix_ratio, info3->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_0, info3->epoch);
	// Process receive block again
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (5, ledger.block_count ());
	// Receiving pruned block (open)
	auto open1 = builder
				 .open ()
				 .source (send2->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ key1.pub, send2->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (6, ledger.block_count ());
	// Rollback open block
	ASSERT_FALSE (ledger.rollback (transaction, open1->hash ()));
	auto info4 = ledger.any.pending_get (transaction, celerix::pending_key (key1.pub, send2->hash ()));
	ASSERT_TRUE (info4);
	ASSERT_NE (celerix::dev::genesis_key.pub, info4->source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (celerix::Kcelerix_ratio, info4->amount.number ());
	ASSERT_EQ (celerix::epoch::epoch_0, info4->epoch);
	// Process open block again
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	ASSERT_FALSE (ledger.any.pending_get (transaction, celerix::pending_key{ key1.pub, send2->hash () }));
	ASSERT_EQ (2, ledger.pruned_count ());
	ASSERT_EQ (6, ledger.block_count ());
}

TEST (ledger, pruning_legacy_blocks)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	celerix::keypair key1;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .send ()
				 .previous (celerix::dev::genesis->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.pending_get (transaction, celerix::pending_key{ celerix::dev::genesis_key.pub, send1->hash () }));
	auto receive1 = builder
					.receive ()
					.previous (send1->hash ())
					.source (send1->hash ())
					.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
					.work (*pool.generate (send1->hash ()))
					.build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, receive1));
	auto change1 = builder
				   .change ()
				   .previous (receive1->hash ())
				   .representative (key1.pub)
				   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				   .work (*pool.generate (receive1->hash ()))
				   .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, change1));
	auto send2 = builder
				 .send ()
				 .previous (change1->hash ())
				 .destination (key1.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (change1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	auto open1 = builder
				 .open ()
				 .source (send2->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .account (key1.pub)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, open1));
	auto send3 = builder
				 .send ()
				 .previous (open1->hash ())
				 .destination (celerix::dev::genesis_key.pub)
				 .balance (0)
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (open1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send3));
	ledger.confirm (transaction, open1->hash ());
	// Pruning action
	ASSERT_EQ (3, ledger.pruning_action (transaction, change1->hash (), 2));
	ASSERT_EQ (1, ledger.pruning_action (transaction, open1->hash (), 1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, receive1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, receive1->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, change1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, change1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ASSERT_FALSE (ledger.any.block_exists (transaction, open1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, open1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send3->hash ()));
	ASSERT_EQ (4, ledger.pruned_count ());
	ASSERT_EQ (7, ledger.block_count ());
	ASSERT_EQ (store->pruned.count (transaction), ledger.pruned_count ());
	ASSERT_EQ (store->block.count (transaction), ledger.block_count () - ledger.pruned_count ());
}

TEST (ledger, pruning_safe_functions)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ledger.confirm (transaction, send1->hash ());
	// Pruning action
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1->hash (), 1));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists_or_pruned (transaction, send1->hash ())); // true for pruned
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	// Safe ledger actions
	ASSERT_FALSE (ledger.any.block_balance (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2, ledger.any.block_balance (transaction, send2->hash ()).value ().number ());
	ASSERT_FALSE (ledger.any.block_amount (transaction, send2->hash ()));
	ASSERT_FALSE (ledger.any.block_account (transaction, send1->hash ()));
	ASSERT_EQ (celerix::dev::genesis_key.pub, ledger.any.block_account (transaction, send2->hash ()).value ());
}

TEST (ledger, random_blocks)
{
	celerix::logger logger;
	auto store = celerix::make_store (logger, celerix::unique_path (), celerix::dev::constants);
	ASSERT_TRUE (!store->init_error ());
	celerix::stats stats{ logger };
	celerix::ledger ledger (*store, stats, celerix::dev::constants);
	ledger.pruning = true;
	auto transaction = ledger.tx_begin_write ();
	store->initialize (transaction, ledger.cache, ledger.constants);
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	celerix::block_builder builder;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send1));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send1->hash ()));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * 2)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ledger.process (transaction, send2));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	ledger.confirm (transaction, send1->hash ());
	// Pruning action
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1->hash (), 1));
	ASSERT_FALSE (ledger.any.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (store->pruned.exists (transaction, send1->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, celerix::dev::genesis->hash ()));
	ASSERT_TRUE (ledger.any.block_exists (transaction, send2->hash ()));
	// Prunned block will not be included in the random selection because it's not in the blocks set
	{
		bool done = false;
		size_t iteration = 0;
		while (!done && iteration < 42)
		{
			++iteration;
			auto blocks = ledger.random_blocks (transaction, 10);
			ASSERT_EQ (blocks.size (), 10); // Random blocks should repeat if the ledger is smaller than the requested count
			auto first = blocks.front ();
			done = (first->hash () == send1->hash ());
		}
		ASSERT_FALSE (done);
	}
	// Genesis and send2 should be included in the random selection
	{
		bool done = false;
		size_t iteration = 0;
		while (!done)
		{
			++iteration;
			auto blocks = ledger.random_blocks (transaction, 1);
			ASSERT_EQ (blocks.size (), 1);
			auto first = blocks.front ();
			done = (first->hash () == send2->hash ());
			ASSERT_LE (iteration, 1000);
		}
	}
	{
		bool done = false;
		size_t iteration = 0;
		while (!done)
		{
			++iteration;
			auto blocks = ledger.random_blocks (transaction, 1);
			ASSERT_EQ (blocks.size (), 1);
			auto first = blocks.front ();
			done = (first->hash () == celerix::dev::genesis->hash ());
			ASSERT_LE (iteration, 1000);
		}
	}
}

TEST (ledger, migrate_lmdb_to_rocksdb)
{
	celerix::test::system system{};
	auto path = celerix::unique_path ();
	celerix::logger logger;
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	celerix::store::lmdb::component store{ logger, path / "data.ldb", celerix::dev::constants };
	celerix::ledger ledger{ store, system.stats, celerix::dev::constants };
	celerix::work_pool pool{ celerix::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	std::shared_ptr<celerix::block> send = celerix::state_block_builder ()
										.account (celerix::dev::genesis_key.pub)
										.previous (celerix::dev::genesis->hash ())
										.representative (0)
										.link (celerix::account (10))
										.balance (celerix::dev::constants.genesis_amount - 100)
										.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
										.work (*pool.generate (celerix::dev::genesis->hash ()))
										.build ();

	celerix::endpoint_key endpoint_key (address.to_bytes (), port);
	auto version = celerix::store::component::version_current;

	{
		auto transaction = ledger.tx_begin_write ();
		store.initialize (transaction, ledger.cache, ledger.constants);
		ASSERT_FALSE (store.init_error ());

		// Lower the database to the max version unsupported for upgrades
		store.confirmation_height.put (transaction, celerix::dev::genesis_key.pub, { 2, send->hash () });

		store.online_weight.put (transaction, 100, celerix::amount (2));
		store.peer.put (transaction, endpoint_key, 37);

		store.pending.put (transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send->hash ()), celerix::pending_info (celerix::dev::genesis_key.pub, 100, celerix::epoch::epoch_0));
		store.pruned.put (transaction, send->hash ());
		store.version.put (transaction, version);
		send->sideband_set ({});
		store.block.put (transaction, send->hash (), *send);
		store.final_vote.put (transaction, send->qualified_root (), celerix::block_hash (2));
	}

	auto error = ledger.migrate_lmdb_to_rocksdb (path);
	ASSERT_FALSE (error);

	celerix::store::rocksdb::component rocksdb_store{ logger, path / "rocksdb", celerix::dev::constants };
	auto rocksdb_transaction (rocksdb_store.tx_begin_read ());

	ASSERT_TRUE (rocksdb_store.pending.get (rocksdb_transaction, celerix::pending_key (celerix::dev::genesis_key.pub, send->hash ())));

	for (auto i = rocksdb_store.online_weight.begin (rocksdb_transaction); i != rocksdb_store.online_weight.end (rocksdb_transaction); ++i)
	{
		ASSERT_EQ (i->first, 100);
		ASSERT_EQ (i->second, 2);
	}

	ASSERT_EQ (rocksdb_store.online_weight.count (rocksdb_transaction), 1);

	auto block1 = rocksdb_store.block.get (rocksdb_transaction, send->hash ());

	ASSERT_EQ (*send, *block1);
	ASSERT_TRUE (rocksdb_store.peer.exists (rocksdb_transaction, endpoint_key));
	ASSERT_EQ (rocksdb_store.version.get (rocksdb_transaction), version);
	celerix::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (rocksdb_store.confirmation_height.get (rocksdb_transaction, celerix::dev::genesis_key.pub, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 2);
	ASSERT_EQ (confirmation_height_info.frontier, send->hash ());
	ASSERT_TRUE (rocksdb_store.final_vote.get (rocksdb_transaction, send->qualified_root ()).has_value ());
	ASSERT_EQ (rocksdb_store.final_vote.get (rocksdb_transaction, send->qualified_root ()).value (), celerix::block_hash (2));

	// Retry migration while rocksdb folder is still present
	auto error_on_retry = ledger.migrate_lmdb_to_rocksdb (path);
	ASSERT_EQ (error_on_retry, true);
}

TEST (ledger, is_send_genesis)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto tx = store.tx_begin_read ();
	ASSERT_FALSE (celerix::dev::genesis->is_send ());
}

TEST (ledger, is_send_state)
{
	auto ctx = celerix::test::ledger_send_receive ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto tx = store.tx_begin_read ();
	ASSERT_TRUE (ctx.blocks ()[0]->is_send ());
	ASSERT_FALSE (ctx.blocks ()[1]->is_send ());
}

TEST (ledger, is_send_legacy)
{
	auto ctx = celerix::test::ledger_send_receive_legacy ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto tx = store.tx_begin_read ();
	ASSERT_TRUE (ctx.blocks ()[0]->is_send ());
	ASSERT_FALSE (ctx.blocks ()[1]->is_send ());
}

TEST (ledger, head_block)
{
	auto ctx = celerix::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & store = ctx.store ();
	auto tx = ledger.tx_begin_read ();
	ASSERT_EQ (*celerix::dev::genesis, *ledger.any.block_get (tx, ledger.any.account_head (tx, celerix::dev::genesis_key.pub)));
}

// Tests that a fairly complex ledger topology can be cemented in a single batch
TEST (ledger, cement_unbounded)
{
	auto ctx = celerix::test::ledger_diamond (5);
	auto & ledger = ctx.ledger ();
	auto bottom = ctx.blocks ().back ();

	std::deque<std::shared_ptr<celerix::block>> confirmed;
	{
		auto tx = ledger.tx_begin_write ();
		confirmed = ledger.confirm (tx, bottom->hash ());
	}
	ASSERT_TRUE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	// Check that all blocks got confirmed in a single call
	ASSERT_TRUE (std::all_of (ctx.blocks ().begin (), ctx.blocks ().end (), [&] (auto const & block) {
		return std::find_if (confirmed.begin (), confirmed.end (), [&] (auto const & block2) {
			return block2->hash () == block->hash ();
		})
		!= confirmed.end ();
	}));
}

// Tests that bounded cementing works when recursion stack is large
TEST (ledger, cement_bounded)
{
	auto ctx = celerix::test::ledger_single_chain (64);
	auto & ledger = ctx.ledger ();
	auto bottom = ctx.blocks ().back ();

	std::deque<std::shared_ptr<celerix::block>> confirmed1, confirmed2, confirmed3;

	{
		// This should only cement some of the dependencies
		auto tx = ledger.tx_begin_write ();
		confirmed1 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 3);
	}
	ASSERT_FALSE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_EQ (confirmed1.size (), 3);
	// Only topmost dependencies should get cemented during this call
	ASSERT_TRUE (std::all_of (confirmed1.begin (), confirmed1.end (), [&] (auto const & block) {
		return ledger.dependents_confirmed (ledger.tx_begin_read (), *block);
	}));

	{
		// This should cement a few more dependencies
		auto tx = ledger.tx_begin_write ();
		confirmed2 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 16);
	}
	ASSERT_FALSE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_EQ (confirmed2.size (), 16);
	// Only topmost dependencies should get cemented during this call
	ASSERT_TRUE (std::all_of (confirmed2.begin (), confirmed2.end (), [&] (auto const & block) {
		return ledger.dependents_confirmed (ledger.tx_begin_read (), *block);
	}));

	{
		// This should cement the remaining dependencies
		auto tx = ledger.tx_begin_write ();
		confirmed3 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 64);
	}
	ASSERT_TRUE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_LE (confirmed3.size (), 64);
	// Every block in the ledger should be cemented
	ASSERT_TRUE (std::all_of (ctx.blocks ().begin (), ctx.blocks ().end (), [&] (auto const & block) {
		return ledger.confirmed.block_exists (ledger.tx_begin_read (), block->hash ());
	}));
}

// Tests that bounded cementing works when number of blocks is large but tree height is small (recursion stack is small)
TEST (ledger, cement_bounded_diamond)
{
	auto ctx = celerix::test::ledger_diamond (4);
	auto & ledger = ctx.ledger ();
	auto bottom = ctx.blocks ().back ();

	std::deque<std::shared_ptr<celerix::block>> confirmed1, confirmed2, confirmed3, confirmed4;

	{
		// This should only cement some of the dependencies
		auto tx = ledger.tx_begin_write ();
		confirmed1 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 3);
	}
	ASSERT_FALSE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_EQ (confirmed1.size (), 3);
	// Only topmost dependencies should get cemented during this call
	ASSERT_TRUE (std::all_of (confirmed1.begin (), confirmed1.end (), [&] (auto const & block) {
		return ledger.dependents_confirmed (ledger.tx_begin_read (), *block);
	}));

	{
		// This should cement a few more dependencies
		auto tx = ledger.tx_begin_write ();
		confirmed2 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 16);
	}
	ASSERT_FALSE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_EQ (confirmed2.size (), 16);
	// Only topmost dependencies should get cemented during this call
	ASSERT_TRUE (std::all_of (confirmed2.begin (), confirmed2.end (), [&] (auto const & block) {
		return ledger.dependents_confirmed (ledger.tx_begin_read (), *block);
	}));

	{
		// A few more bounded calls should confirm the whole tree
		auto tx = ledger.tx_begin_write ();
		confirmed3 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 64);
	}
	ASSERT_FALSE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_EQ (confirmed3.size (), 64);
	// Only topmost dependencies should get cemented during this call
	ASSERT_TRUE (std::all_of (confirmed2.begin (), confirmed2.end (), [&] (auto const & block) {
		return ledger.dependents_confirmed (ledger.tx_begin_read (), *block);
	}));

	{
		auto tx = ledger.tx_begin_write ();
		confirmed4 = ledger.confirm (tx, bottom->hash (), /* max cementing batch size */ 64);
	}
	ASSERT_TRUE (ledger.confirmed.block_exists (ledger.tx_begin_read (), bottom->hash ()));
	ASSERT_LT (confirmed4.size (), 64);
	// Every block in the ledger should be cemented
	ASSERT_TRUE (std::all_of (ctx.blocks ().begin (), ctx.blocks ().end (), [&] (auto const & block) {
		return ledger.confirmed.block_exists (ledger.tx_begin_read (), block->hash ());
	}));
}

// Test that nullopt can be returned when there are no receivable entries
TEST (ledger_receivable, upper_bound_account_none)
{
	auto ctx = celerix::test::ledger_empty ();
	ASSERT_EQ (ctx.ledger ().any.receivable_end (), ctx.ledger ().any.receivable_upper_bound (ctx.ledger ().tx_begin_read (), 0));
}

// Test behavior of ledger::receivable_upper_bound when there are receivable entries for multiple accounts
TEST (ledger_receivable, upper_bound_account_key)
{
	auto ctx = celerix::test::ledger_empty ();
	celerix::block_builder builder;
	celerix::keypair key;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (ctx.ledger ().tx_begin_write (), send1));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (ctx.ledger ().tx_begin_write (), send2));
	auto tx = ctx.ledger ().tx_begin_read ();
	auto & ledger = ctx.ledger ();
	auto next1 = ledger.any.receivable_upper_bound (tx, celerix::dev::genesis_key.pub);
	auto next2 = ledger.any.receivable_upper_bound (tx, key.pub);
	// Depending on which is greater but only one should have a value
	ASSERT_TRUE (next1 == ledger.any.receivable_end () xor next2 == ledger.any.receivable_end ());
	// The account returned should be after the one we searched for
	ASSERT_TRUE (next1 == ledger.any.receivable_end () || next1->first.account == key.pub);
	ASSERT_TRUE (next2 == ledger.any.receivable_end () || next2->first.account == celerix::dev::genesis_key.pub);
	auto next3 = ledger.any.receivable_upper_bound (tx, celerix::dev::genesis_key.pub, 0);
	auto next4 = ledger.any.receivable_upper_bound (tx, key.pub, 0);
	// Neither account has more than one receivable
	ASSERT_TRUE (next3 != ledger.any.receivable_end () && next4 != ledger.any.receivable_end ());
	auto next5 = ledger.any.receivable_upper_bound (tx, next3->first.account, next3->first.hash);
	auto next6 = ledger.any.receivable_upper_bound (tx, next4->first.account, next4->first.hash);
	ASSERT_TRUE (next5 == ledger.any.receivable_end () && next6 == ledger.any.receivable_end ());
	ASSERT_EQ (ledger.any.receivable_end (), ++next3);
	ASSERT_EQ (ledger.any.receivable_end (), ++next4);
}

// Test that multiple receivable entries for the same account
TEST (ledger_receivable, key_two)
{
	auto ctx = celerix::test::ledger_empty ();
	celerix::block_builder builder;
	celerix::keypair key;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (ctx.ledger ().tx_begin_write (), send1));
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (send1->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (ctx.ledger ().tx_begin_write (), send2));
	auto tx = ctx.ledger ().tx_begin_read ();
	auto & ledger = ctx.ledger ();
	auto next1 = ledger.any.receivable_upper_bound (tx, key.pub, 0);
	ASSERT_TRUE (next1 != ledger.any.receivable_end () && next1->first.account == key.pub);
	auto next2 = ledger.any.receivable_upper_bound (tx, key.pub, next1->first.hash);
	ASSERT_TRUE (next2 != ledger.any.receivable_end () && next2->first.account == key.pub);
	ASSERT_NE (next1->first.hash, next2->first.hash);
	ASSERT_EQ (next2, ++next1);
	ASSERT_EQ (ledger.any.receivable_end (), ++next1);
	ASSERT_EQ (ledger.any.receivable_end (), ++next2);
}

TEST (ledger_receivable, any_none)
{
	auto ctx = celerix::test::ledger_empty ();
	ASSERT_FALSE (ctx.ledger ().any.receivable_exists (ctx.ledger ().tx_begin_read (), celerix::dev::genesis_key.pub));
}

TEST (ledger_receivable, any_one)
{
	auto ctx = celerix::test::ledger_empty ();
	celerix::block_builder builder;
	celerix::keypair key;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (celerix::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (ctx.ledger ().tx_begin_write (), send1));
	ASSERT_TRUE (ctx.ledger ().any.receivable_exists (ctx.ledger ().tx_begin_read (), celerix::dev::genesis_key.pub));
	ASSERT_FALSE (ctx.ledger ().any.receivable_exists (ctx.ledger ().tx_begin_read (), key.pub));
}

TEST (ledger_transaction, write_refresh)
{
	auto ctx = celerix::test::ledger_empty ();
	celerix::block_builder builder;
	celerix::keypair key;
	auto send1 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (celerix::dev::genesis->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio)
				 .link (celerix::dev::genesis_key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (celerix::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder
				 .state ()
				 .account (celerix::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (celerix::dev::genesis_key.pub)
				 .balance (celerix::dev::constants.genesis_amount - 2 * celerix::Kcelerix_ratio)
				 .link (key.pub)
				 .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				 .work (*ctx.pool ().generate (send1->hash ()))
				 .build ();

	auto transaction = ctx.ledger ().tx_begin_write ();
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (transaction, send1));
	// Force refresh
	ASSERT_TRUE (transaction.refresh_if_needed (0ms));
	ASSERT_FALSE (transaction.refresh_if_needed ()); // Should not refresh again too soon
	// Refreshed transaction should work just fine
	ASSERT_EQ (celerix::block_status::progress, ctx.ledger ().process (transaction, send2));
}

TEST (ledger_transaction, write_wait_order)
{
	celerix::test::system system;

	auto ctx = celerix::test::ledger_empty ();

	std::atomic<bool> acquired1{ false };
	std::atomic<bool> acquired2{ false };
	std::atomic<bool> acquired3{ false };

	std::latch latch1{ 1 };
	std::latch latch2{ 1 };
	std::latch latch3{ 1 };

	auto fut1 = std::async (std::launch::async, [&] {
		auto tx = ctx.ledger ().tx_begin_write (celerix::store::writer::generic);
		acquired1 = true;
		latch1.wait (); // Wait for the signal to drop tx
	});
	WAIT (250ms); // Allow thread to start

	auto fut2 = std::async (std::launch::async, [&ctx, &acquired2, &latch2] {
		auto tx = ctx.ledger ().tx_begin_write (celerix::store::writer::block_processor);
		acquired2 = true;
		latch2.wait (); // Wait for the signal to drop tx
	});
	WAIT (250ms); // Allow thread to start

	auto fut3 = std::async (std::launch::async, [&ctx, &acquired3, &latch3] {
		auto tx = ctx.ledger ().tx_begin_write (celerix::store::writer::confirmation_height);
		acquired3 = true;
		latch3.wait (); // Wait for the signal to drop tx
	});
	WAIT (250ms); // Allow thread to start

	// First transaction should be ready immediately, others should be waiting
	ASSERT_TIMELY (5s, acquired1.load ());
	ASSERT_NEVER (250ms, acquired2.load ());
	ASSERT_NEVER (250ms, acquired3.load ());

	// Signal to continue and drop the first transaction
	latch1.count_down ();
	ASSERT_TIMELY (5s, acquired2.load ());
	ASSERT_NEVER (250ms, acquired3.load ());

	// Signal to continue and drop the second transaction
	latch2.count_down ();
	ASSERT_TIMELY (5s, acquired3.load ());

	// Signal to continue and drop the third transaction
	latch3.count_down ();
}

TEST (ledger_transaction, multithreaded_interleaving)
{
	celerix::test::system system;

	auto ctx = celerix::test::ledger_empty ();

	int constexpr num_threads = 2;
	int constexpr num_iterations = 10;
	int constexpr num_blocks = 10;

	std::deque<std::thread> threads;
	for (int i = 0; i < num_threads; ++i)
	{
		threads.emplace_back ([&] {
			for (int n = 0; n < num_iterations; ++n)
			{
				auto tx = ctx.ledger ().tx_begin_write (celerix::store::writer::testing);
				for (unsigned k = 0; k < num_blocks; ++k)
				{
					ctx.store ().account.put (tx, celerix::account{ k }, celerix::account_info{});
				}
				for (unsigned k = 0; k < num_blocks; ++k)
				{
					ctx.store ().account.del (tx, celerix::account{ k });
				}
			}
		});
	}

	for (auto & thread : threads)
	{
		thread.join ();
	}
}