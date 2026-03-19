#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/secure/voting_policy.hpp>
#include <nano/store/ledger/final_vote.hpp>
#include <nano/test_common/ledger_context.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

/*
 * vote_normal
 */

// Genesis block has zero dependencies — always eligible
TEST (voting_policy, vote_normal_genesis)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	nano::voting_policy policy{ ledger };

	auto txn = ledger.tx_begin_read ();
	auto result = policy.vote_normal (txn, *nano::dev::genesis);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->qualified_root (), nano::dev::genesis->qualified_root ());
	ASSERT_EQ (result->hash (), nano::dev::genesis->hash ());
	ASSERT_EQ (result->type (), nano::vote_type::normal);
}

// Send from genesis — previous (genesis) is cemented at init
TEST (voting_policy, vote_normal_send)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	auto result = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->qualified_root (), send1->qualified_root ());
	ASSERT_EQ (result->hash (), send1->hash ());
	ASSERT_EQ (result->type (), nano::vote_type::normal);
}

// Second send fails — previous (send1) is not cemented
TEST (voting_policy, vote_normal_send_previous_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));

	ASSERT_FALSE (policy.vote_normal (txn, *send2));
	ASSERT_FALSE (policy.vote_final (txn, *send2));
	ASSERT_FALSE (policy.reply_final (txn, *send2));
}

// Open block receiving from uncemented send — source dependency not met
TEST (voting_policy, vote_normal_open_source_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));

	// send1 is not cemented so open1's source dependency fails
	ASSERT_FALSE (policy.vote_normal (txn, *open1));
	ASSERT_FALSE (policy.vote_final (txn, *open1));
	ASSERT_FALSE (policy.reply_final (txn, *open1));
}

// Open block receiving from cemented send — previous is zero (trivial), source cemented
TEST (voting_policy, vote_normal_open_source_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ledger.cement (txn, send1->hash ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));

	auto result = policy.vote_normal (txn, *open1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->qualified_root (), open1->qualified_root ());
	ASSERT_EQ (result->hash (), open1->hash ());
}

// Receive where previous is cemented but source send is not
TEST (voting_policy, vote_normal_receive_previous_cemented_source_not)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	// send1 to key1 — cement it so open1 can be cemented
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	// send2 to key1 — do NOT cement
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	// open1 for key1 receiving send1
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	// receive2 on key1 receiving send2 (previous=open1, source=send2)
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ledger.cement (txn, send1->hash ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ledger.cement (txn, open1->hash ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));

	// previous (open1) is cemented, but source (send2) is not
	ASSERT_FALSE (policy.vote_normal (txn, *receive2));
	ASSERT_FALSE (policy.vote_final (txn, *receive2));
	ASSERT_FALSE (policy.reply_final (txn, *receive2));
}

// Receive where source is cemented but previous is not
TEST (voting_policy, vote_normal_receive_source_cemented_previous_not)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	// Two sends from genesis
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	// open1 for key1 receiving send1 — do NOT cement
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	// receive2 on key1 receiving send2
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ledger.cement (txn, send2->hash ()); // cements send1 and send2
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));

	// source (send2) is cemented, but previous (open1) is not
	ASSERT_FALSE (policy.vote_normal (txn, *receive2));
	ASSERT_FALSE (policy.vote_final (txn, *receive2));
	ASSERT_FALSE (policy.reply_final (txn, *receive2));
}

// Receive with both previous and source cemented — eligible
TEST (voting_policy, vote_normal_receive_both_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));

	// Cement all dependencies
	ledger.cement (txn, send2->hash ()); // cements genesis, send1, send2
	ledger.cement (txn, open1->hash ()); // cements open1

	auto result = policy.vote_normal (txn, *receive2);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->hash (), receive2->hash ());
}

// Change block with cemented previous — eligible (no source dependency)
TEST (voting_policy, vote_normal_change_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair new_rep;
	nano::block_builder builder;

	auto change1 = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (new_rep.pub)
				   .balance (nano::dev::constants.genesis_amount)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*pool.generate (nano::dev::genesis->hash ()))
				   .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), change1));

	auto txn = ledger.tx_begin_read ();
	auto result = policy.vote_normal (txn, *change1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->hash (), change1->hash ());
}

// Change block with uncemented previous — not eligible
TEST (voting_policy, vote_normal_change_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, new_rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto change1 = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send1->hash ())
				   .representative (new_rep.pub)
				   .balance (nano::dev::constants.genesis_amount - 100)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (*pool.generate (send1->hash ()))
				   .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, change1));

	// send1 not cemented, so change1 fails
	ASSERT_FALSE (policy.vote_normal (txn, *change1));
	ASSERT_FALSE (policy.vote_final (txn, *change1));
	ASSERT_FALSE (policy.reply_final (txn, *change1));
}

// Epoch block with cemented previous — eligible (link is epoch link, not a block hash)
TEST (voting_policy, vote_normal_epoch_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::block_builder builder;

	auto epoch1 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount)
				  .link (ledger.epoch_link (nano::epoch::epoch_1))
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch1));

	auto result = policy.vote_normal (txn, *epoch1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->hash (), epoch1->hash ());

	auto final_result = policy.vote_final (txn, *epoch1);
	ASSERT_TRUE (final_result);
	ASSERT_EQ (final_result->type (), nano::vote_type::final);
}

// Epoch block with uncemented previous — not eligible
TEST (voting_policy, vote_normal_epoch_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto epoch1 = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (send1->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 100)
				  .link (ledger.epoch_link (nano::epoch::epoch_1))
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (send1->hash ()))
				  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch1));

	// send1 not cemented
	ASSERT_FALSE (policy.vote_normal (txn, *epoch1));
	ASSERT_FALSE (policy.vote_final (txn, *epoch1));
	ASSERT_FALSE (policy.reply_final (txn, *epoch1));
}

// Epoch open block — dependencies are [0, 0], always eligible
TEST (voting_policy, vote_normal_epoch_open_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	// Send to key1 so there's a pending entry (required for epoch open)
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	// Epoch open for key1 — previous=0, balance=0, signed by epoch signer
	auto epoch_open = builder.state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch_open));

	// Epoch open has deps [0, 0] — always eligible regardless of send1 cementing status
	auto result = policy.vote_normal (txn, *epoch_open);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->hash (), epoch_open->hash ());

	auto final_result = policy.vote_final (txn, *epoch_open);
	ASSERT_TRUE (final_result);
	ASSERT_EQ (final_result->hash (), epoch_open->hash ());
	ASSERT_EQ (final_result->qualified_root (), epoch_open->qualified_root ());
	ASSERT_EQ (final_result->type (), nano::vote_type::final);
}

// Epoch open block with uncemented send — eligible because deps are [0, 0]
TEST (voting_policy, vote_normal_epoch_open_send_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto epoch_open = builder.state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (ledger.epoch_link (nano::epoch::epoch_1))
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (key1.pub))
					  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, epoch_open));

	// send1 is NOT cemented, but epoch open deps are [0, 0] — eligible
	ASSERT_TRUE (policy.vote_normal (txn, *epoch_open));
	ASSERT_TRUE (policy.vote_final (txn, *epoch_open));
}

/*
 * vote_final
 */

// Dependencies cemented — permit granted and final_vote record written
TEST (voting_policy, vote_final_eligible)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));

	ASSERT_FALSE (policy.reply_final (txn, *send1)); // vote_final must be called before reply_final

	auto result = policy.vote_final (txn, *send1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->type (), nano::vote_type::final);
	ASSERT_EQ (result->hash (), send1->hash ());

	// Verify the final_vote record was persisted
	auto stored = ledger.store.final_vote.get (txn, send1->qualified_root ());
	ASSERT_TRUE (stored);
	ASSERT_EQ (*stored, send1->hash ());

	ASSERT_TRUE (policy.reply_final (txn, *send1));
}

// Dependencies not cemented — no permit, no record written
TEST (voting_policy, vote_final_not_eligible)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));

	// send1 not cemented, so send2 not eligible
	ASSERT_FALSE (policy.vote_final (txn, *send2));
	ASSERT_FALSE (policy.reply_final (txn, *send2));

	// No record should have been written (short-circuit evaluation)
	auto stored = ledger.store.final_vote.get (txn, send2->qualified_root ());
	ASSERT_FALSE (stored);
}

// Calling vote_final twice for the same block — idempotent (put returns true for same hash)
TEST (voting_policy, vote_final_idempotent)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));

	auto result1 = policy.vote_final (txn, *send1);
	auto result2 = policy.vote_final (txn, *send1);
	ASSERT_TRUE (result1);
	ASSERT_TRUE (result2);
}

// Final vote for fork is prevented — different block at same qualified_root is rejected
TEST (voting_policy, vote_final_fork_prevented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, key2;
	nano::block_builder builder;

	// Process send1A and record final vote
	auto send1a = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 100)
				  .link (key1.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();
	// Fork: different send from same previous (different destination/amount)
	auto send1b = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 200)
				  .link (key2.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1a));

	// Record final vote for send1a
	ASSERT_TRUE (policy.vote_final (txn, *send1a));
	ASSERT_TRUE (policy.reply_final (txn, *send1a));

	// Rollback send1a and process fork send1b
	ASSERT_FALSE (ledger.rollback (txn, send1a->hash ()));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1b));

	// Final vote for fork must be denied — the record already has send1a's hash
	ASSERT_FALSE (policy.vote_final (txn, *send1b));
	// Reply returns permit with the originally recorded hash, not the fork's
	auto reply = policy.reply_final (txn, *send1b);
	ASSERT_TRUE (reply);
	ASSERT_EQ (reply->hash (), send1a->hash ());
}

/*
 * reply_final
 */

// Read-only final reply check — eligible when dependencies cemented and block is final-votable
TEST (voting_policy, reply_final_eligible)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));

	// Record final vote so reply_final returns true
	auto final_permit = policy.vote_final (txn, *send1);
	ASSERT_TRUE (final_permit);

	// Read-only reply check should succeed
	auto result = policy.reply_final (txn, *send1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->type (), nano::vote_type::final);
	ASSERT_EQ (result->hash (), send1->hash ());

	// Sign with the reply permit and verify the vote
	nano::keypair rep;
	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	auto votes = policy.sign (nano::vote_type::final, { *result }, signer);
	ASSERT_EQ (votes.size (), 1);
	ASSERT_TRUE (votes[0]->is_final ());
	ASSERT_EQ (votes[0]->hashes.size (), 1);
	ASSERT_EQ (votes[0]->hashes[0], send1->hash ());
	ASSERT_EQ (votes[0]->account, rep.pub);
	ASSERT_FALSE (votes[0]->validate ());
}

// No final vote record and block not cemented — no reply
TEST (voting_policy, reply_final_no_record_not_cemented)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	ASSERT_FALSE (policy.reply_final (txn, *send1));
}

// Fork query returns permit with the originally recorded hash, not the fork's hash
TEST (voting_policy, reply_final_fork_returns_recorded_hash)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, key2, rep;
	nano::block_builder builder;

	auto send1a = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 100)
				  .link (key1.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();
	auto send1b = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 200)
				  .link (key2.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1a));

	// Record final vote for send1a
	ASSERT_TRUE (policy.vote_final (txn, *send1a));

	// Replace with fork
	ASSERT_FALSE (ledger.rollback (txn, send1a->hash ()));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1b));

	// Querying the fork returns a permit with the RECORDED hash (send1a), not the fork's hash
	auto result = policy.reply_final (txn, *send1b);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->hash (), send1a->hash ());
	ASSERT_NE (result->hash (), send1b->hash ());
	ASSERT_EQ (result->type (), nano::vote_type::final);

	// Signing with this permit produces a vote for the originally committed block
	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	auto votes = policy.sign (nano::vote_type::final, { *result }, signer);
	ASSERT_EQ (votes.size (), 1);
	ASSERT_TRUE (votes[0]->is_final ());
	ASSERT_EQ (votes[0]->hashes[0], send1a->hash ());
	ASSERT_FALSE (votes[0]->validate ());
}

// No final vote record but block is cemented — reply with block's hash
TEST (voting_policy, reply_final_cemented_fallback)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ledger.cement (txn, send1->hash ());

	auto result = policy.reply_final (txn, *send1);
	ASSERT_TRUE (result);
	ASSERT_EQ (result->type (), nano::vote_type::final);
	ASSERT_EQ (result->hash (), send1->hash ());
}

/*
 * signing
 */

// Mixing normal and final permits in sign() triggers release_assert
TEST (voting_policy_DeathTest, sign_type_mismatch)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));

	auto normal_permit = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (normal_permit);

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };

	// Passing normal permits with vote_type::final must crash
	ASSERT_DEATH_IF_SUPPORTED (policy.sign (nano::vote_type::final, { *normal_permit }, signer), "");
}

// Empty permits returns empty votes — signer is not called
TEST (voting_policy, sign_empty)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	nano::voting_policy policy{ ledger };

	bool signer_called = false;
	nano::voting_policy::vote_signer_t signer = [&signer_called] (auto const & callback) {
		signer_called = true;
		nano::keypair key;
		callback (key.pub, key.prv);
	};

	auto votes = policy.sign (nano::vote_type::normal, {}, signer);
	ASSERT_TRUE (votes.empty ());
	ASSERT_FALSE (signer_called);
}

// Single permit, single signer — produces one vote with correct fields and valid signature
TEST (voting_policy, sign_single_permit)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	auto permit = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (permit);

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	nano::millis_t ts = 99008; // 16-byte aligned for clean assertion
	auto votes = policy.sign (nano::vote_type::normal, { *permit }, signer, ts);

	ASSERT_EQ (votes.size (), 1);
	ASSERT_EQ (votes[0]->hashes.size (), 1);
	ASSERT_EQ (votes[0]->hashes[0], send1->hash ());
	ASSERT_EQ (votes[0]->account, rep.pub);
	ASSERT_EQ (votes[0]->timestamp (), ts & nano::vote::timestamp_mask);
	ASSERT_EQ (votes[0]->duration_bits (), 0x9);
	ASSERT_FALSE (votes[0]->is_final ());
	ASSERT_FALSE (votes[0]->validate ()); // validate() returns false when signature is valid
}

// Multiple permits batched into a single vote with all hashes
TEST (voting_policy, sign_multiple_permits)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ledger.cement (txn, send1->hash ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));

	auto permit1 = policy.vote_normal (txn, *send1);
	auto permit2 = policy.vote_normal (txn, *send2);
	ASSERT_TRUE (permit1);
	ASSERT_TRUE (permit2);

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	auto votes = policy.sign (nano::vote_type::normal, { *permit1, *permit2 }, signer, 99008);

	ASSERT_EQ (votes.size (), 1);
	ASSERT_EQ (votes[0]->hashes.size (), 2);
	ASSERT_EQ (votes[0]->hashes[0], send1->hash ());
	ASSERT_EQ (votes[0]->hashes[1], send2->hash ());
	ASSERT_FALSE (votes[0]->validate ());
}

// Multiple signers produce one vote per representative
TEST (voting_policy, sign_multiple_signers)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep1, rep2;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	auto permit = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (permit);

	nano::voting_policy::vote_signer_t signer = [&rep1, &rep2] (auto const & callback) {
		callback (rep1.pub, rep1.prv);
		callback (rep2.pub, rep2.prv);
	};
	auto votes = policy.sign (nano::vote_type::normal, { *permit }, signer, 99008);

	ASSERT_EQ (votes.size (), 2);
	ASSERT_EQ (votes[0]->account, rep1.pub);
	ASSERT_EQ (votes[1]->account, rep2.pub);
	ASSERT_FALSE (votes[0]->validate ());
	ASSERT_FALSE (votes[1]->validate ());
}

// Final vote type produces max timestamp and max duration
TEST (voting_policy, sign_final_type)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	auto permit = policy.vote_final (txn, *send1);
	ASSERT_TRUE (permit);

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	auto votes = policy.sign (nano::vote_type::final, { *permit }, signer);

	ASSERT_EQ (votes.size (), 1);
	ASSERT_TRUE (votes[0]->is_final ());
	ASSERT_EQ (votes[0]->duration_bits (), nano::vote::duration_max);
	ASSERT_FALSE (votes[0]->validate ());
}

// Custom timestamp is forwarded correctly for normal votes
TEST (voting_policy, sign_normal_custom_timestamp)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	auto permit = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (permit);

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };

	// Test with a non-aligned timestamp to verify masking behavior
	nano::millis_t ts = 12345;
	auto votes = policy.sign (nano::vote_type::normal, { *permit }, signer, ts);

	ASSERT_EQ (votes.size (), 1);
	ASSERT_EQ (votes[0]->timestamp (), ts & nano::vote::timestamp_mask);
	ASSERT_FALSE (votes[0]->is_final ());
}

// Empty signer (no representatives) produces no votes
TEST (voting_policy, sign_no_signers)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (ledger.tx_begin_write (), send1));

	auto txn = ledger.tx_begin_read ();
	auto permit = policy.vote_normal (txn, *send1);
	ASSERT_TRUE (permit);

	nano::voting_policy::vote_signer_t empty_signer = [] (auto const &) {};
	auto votes = policy.sign (nano::vote_type::normal, { *permit }, empty_signer, 99008);

	ASSERT_TRUE (votes.empty ());
}

/*
 * Integration / cross-cutting
 */

// Incrementally cement dependencies and verify eligibility changes at each step
TEST (voting_policy, progressive_cementing)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1;
	nano::block_builder builder;

	// Build chain: genesis → send1 → send2 (genesis account)
	//              send1 → open1 (key1 account)
	//              send2 → receive2 (key1 account, previous=open1)
	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (open1->hash ())
					.representative (nano::dev::genesis_key.pub)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (open1->hash ()))
					.build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, receive2));

	// Phase 1: Nothing cemented beyond genesis — receive2 not eligible
	ASSERT_FALSE (policy.vote_normal (txn, *receive2));
	ASSERT_FALSE (policy.vote_normal (txn, *open1));

	// Phase 2: Cement send1 — open1 now eligible (source cemented), receive2 still not
	ledger.cement (txn, send1->hash ());
	ASSERT_TRUE (policy.vote_normal (txn, *open1));
	ASSERT_FALSE (policy.vote_normal (txn, *receive2));

	// Phase 3: Cement open1 — receive2 still not eligible (send2 not cemented)
	ledger.cement (txn, open1->hash ());
	ASSERT_FALSE (policy.vote_normal (txn, *receive2));

	// Phase 4: Cement send2 — receive2 now eligible (both deps cemented)
	ledger.cement (txn, send2->hash ());
	ASSERT_TRUE (policy.vote_normal (txn, *receive2));
}

// Complete flow: check eligibility, get permits, sign votes, verify signatures
TEST (voting_policy, full_send_receive_sign)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, rep;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ledger.cement (txn, send1->hash ());
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));

	// Check normal eligibility
	auto normal_permit = policy.vote_normal (txn, *open1);
	ASSERT_TRUE (normal_permit);

	// Check final eligibility
	auto final_permit = policy.vote_final (txn, *open1);
	ASSERT_TRUE (final_permit);

	// reply_final should also return true
	ASSERT_TRUE (policy.reply_final (txn, *open1));

	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };

	// Sign normal vote
	auto normal_votes = policy.sign (nano::vote_type::normal, { *normal_permit }, signer, 99008);
	ASSERT_EQ (normal_votes.size (), 1);
	ASSERT_FALSE (normal_votes[0]->is_final ());
	ASSERT_FALSE (normal_votes[0]->validate ());

	// Sign final vote
	auto final_votes = policy.sign (nano::vote_type::final, { *final_permit }, signer);
	ASSERT_EQ (final_votes.size (), 1);
	ASSERT_TRUE (final_votes[0]->is_final ());
	ASSERT_FALSE (final_votes[0]->validate ());
}

// Pruned source dependency counts as cemented
TEST (voting_policy, pruned_dependency)
{
	nano::logger logger;
	nano::stats stats{ logger };
	auto store = nano::make_store (logger, stats, nano::unique_path (), nano::dev::constants);
	nano::ledger ledger (*store, nano::dev::network_params, stats, logger);
	ledger.pruning = true;
	nano::voting_policy policy{ ledger };
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	nano::keypair key1;
	nano::block_builder builder;

	auto send1 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 100)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 200)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send2));
	ledger.cement (txn, send2->hash ());

	// Prune send1 (it's cemented, so prunable)
	ASSERT_EQ (2, ledger.pruning_action (txn, send2->hash (), 1));

	// open1 receiving from pruned send1
	auto open1 = builder.state ()
				 .account (key1.pub)
				 .previous (0)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (100)
				 .link (send1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (key1.pub))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, open1));

	// Pruned source should satisfy dependency check
	ASSERT_TRUE (policy.vote_normal (txn, *open1));
	ASSERT_TRUE (policy.vote_final (txn, *open1));
	ASSERT_TRUE (policy.reply_final (txn, *open1));
}

// Final vote record prevents voting on competing fork across all methods
TEST (voting_policy, final_vote_fork_safety)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto & pool = ctx.pool ();
	nano::voting_policy policy{ ledger };
	nano::keypair key1, key2, rep;
	nano::block_builder builder;

	auto send1a = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 100)
				  .link (key1.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();
	auto send1b = builder.state ()
				  .account (nano::dev::genesis_key.pub)
				  .previous (nano::dev::genesis->hash ())
				  .representative (nano::dev::genesis_key.pub)
				  .balance (nano::dev::constants.genesis_amount - 200)
				  .link (key2.pub)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*pool.generate (nano::dev::genesis->hash ()))
				  .build ();

	auto txn = ledger.tx_begin_write ();
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1a));

	// Commit final vote for send1a
	auto permit_a = policy.vote_final (txn, *send1a);
	ASSERT_TRUE (permit_a);

	// Sign the final vote successfully
	nano::voting_policy::vote_signer_t signer = [&rep] (auto const & callback) { callback (rep.pub, rep.prv); };
	auto votes_a = policy.sign (nano::vote_type::final, { *permit_a }, signer);
	ASSERT_EQ (votes_a.size (), 1);
	ASSERT_TRUE (votes_a[0]->is_final ());

	// reply_final confirms send1a
	ASSERT_TRUE (policy.reply_final (txn, *send1a));

	// Replace with fork
	ASSERT_FALSE (ledger.rollback (txn, send1a->hash ()));
	ASSERT_EQ (nano::block_status::progress, ledger.process (txn, send1b));

	// vote_final denies the fork
	ASSERT_FALSE (policy.vote_final (txn, *send1b));
	// reply_final returns permit with the originally recorded hash
	auto fork_reply = policy.reply_final (txn, *send1b);
	ASSERT_TRUE (fork_reply);
	ASSERT_EQ (fork_reply->hash (), send1a->hash ());

	// Normal vote is also denied for the fork
	ASSERT_FALSE (policy.vote_normal (txn, *send1b));

	// Rollback fork and confirm original block is still votable
	ASSERT_FALSE (ledger.rollback (txn, send1b->hash ()));
	ASSERT_TRUE (policy.reply_final (txn, *send1a));
	ASSERT_TRUE (policy.vote_final (txn, *send1a));
}
