#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/secure/state_commitment.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/test_common/ledger_context.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// A fresh ledger holds only the cemented genesis account and no pending entries.
TEST (state_commitment, genesis_only)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	auto transaction = ledger.tx_begin_read ();
	auto commitment = nano::compute_state_commitment (ledger, transaction);
	ASSERT_EQ (1, commitment.account_count);
	ASSERT_EQ (0, commitment.pending_count);
	ASSERT_FALSE (commitment.root.is_zero ());
}

// The commitment is a pure function of state: recomputing yields byte-identical roots.
TEST (state_commitment, deterministic)
{
	auto ctx = nano::test::ledger_send_receive ();
	auto & ledger = ctx.ledger ();
	{
		auto write = ledger.tx_begin_write ();
		for (auto const & block : ctx.blocks ())
		{
			ledger.cement (write, block->hash ());
		}
	}
	auto transaction = ledger.tx_begin_read ();
	auto first = nano::compute_state_commitment (ledger, transaction);
	auto second = nano::compute_state_commitment (ledger, transaction);
	ASSERT_EQ (first.root, second.root);
	ASSERT_EQ (first.accounts_root, second.accounts_root);
	ASSERT_EQ (first.pending_root, second.pending_root);
	// Send + receive on genesis leaves no live pending entry.
	ASSERT_EQ (1, first.account_count);
	ASSERT_EQ (0, first.pending_count);
}

// A dust send to a never-opened account adds exactly one permanent pending entry;
// the commitment must reflect it: pending_count rises and the root changes.
TEST (state_commitment, dust_pending_changes_root)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();

	nano::block_hash baseline_root;
	{
		auto transaction = ledger.tx_begin_read ();
		baseline_root = nano::compute_state_commitment (ledger, transaction).root;
	}

	nano::keypair destination;
	nano::block_builder builder;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto dust = builder.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (destination.pub) // 32 arbitrary bytes: the abuse vector
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();

	{
		auto write = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (write, dust));
		ledger.cement (write, dust->hash ());
	}

	auto transaction = ledger.tx_begin_read ();
	auto after = nano::compute_state_commitment (ledger, transaction);
	ASSERT_EQ (1, after.pending_count);
	ASSERT_NE (baseline_root, after.root);
	ASSERT_FALSE (after.pending_root.is_zero ());
}

// An account proof reconstructs the commitment root and verifies; tampering fails.
TEST (state_commitment, account_proof_roundtrip)
{
	auto ctx = nano::test::ledger_send_receive ();
	auto & ledger = ctx.ledger ();
	{
		auto write = ledger.tx_begin_write ();
		for (auto const & block : ctx.blocks ())
		{
			ledger.cement (write, block->hash ());
		}
	}
	auto transaction = ledger.tx_begin_read ();
	auto commitment = nano::compute_state_commitment (ledger, transaction);
	auto proof = nano::generate_account_proof (ledger, transaction, nano::dev::genesis_key.pub);
	ASSERT_TRUE (proof.has_value ());
	// The proof reconstructs the exact same root the commitment produced.
	ASSERT_EQ (commitment.root, proof->root);
	ASSERT_TRUE (nano::verify_state_proof (proof.value ()));

	// Tampering with the claimed balance must break verification.
	auto tampered = proof.value ();
	tampered.account_claim->balance = tampered.account_claim->balance.number () + 1;
	ASSERT_FALSE (nano::verify_state_proof (tampered));

	// A non-existent account yields no proof.
	nano::keypair absent;
	ASSERT_FALSE (nano::generate_account_proof (ledger, transaction, absent.pub).has_value ());
}

// A pending proof over the dust receivable reconstructs the root and verifies.
TEST (state_commitment, pending_proof_roundtrip)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	nano::keypair destination;
	nano::block_builder builder;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	auto dust = builder.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1)
				.link (destination.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*pool.generate (nano::dev::genesis->hash ()))
				.build ();
	{
		auto write = ledger.tx_begin_write ();
		ASSERT_EQ (nano::block_status::progress, ledger.process (write, dust));
		ledger.cement (write, dust->hash ());
	}
	auto transaction = ledger.tx_begin_read ();
	auto commitment = nano::compute_state_commitment (ledger, transaction);
	auto proof = nano::generate_pending_proof (ledger, transaction, nano::pending_key{ destination.pub, dust->hash () });
	ASSERT_TRUE (proof.has_value ());
	ASSERT_EQ (commitment.root, proof->root);
	ASSERT_TRUE (nano::verify_state_proof (proof.value ()));
	ASSERT_TRUE (proof->pending_claim.has_value ());
	ASSERT_EQ (destination.pub, proof->pending_claim->account);
}

// The retention planner classifies droppable vs kept blocks for a per-account window,
// proves every retained account against the checkpoint root, and quantifies reclaim.
TEST (state_commitment, retention_plan)
{
	auto ctx = nano::test::ledger_single_chain (16); // 16 blocks on genesis account
	auto & ledger = ctx.ledger ();
	{
		auto write = ledger.tx_begin_write ();
		ledger.cement (write, ctx.blocks ().back ()->hash ());
	}
	auto transaction = ledger.tx_begin_read ();
	auto commitment = nano::compute_state_commitment (ledger, transaction);

	// Genesis account cemented frontier height = 1 (genesis) + 16 built blocks.
	uint64_t const total_height = ledger.cemented.account_height (transaction, nano::dev::genesis_key.pub);
	ASSERT_EQ (17, total_height);

	auto plan = nano::plan_capped_retention (ledger, transaction, 5, 0);
	// Checkpoint anchors to the same root the commitment computes.
	ASSERT_EQ (commitment.root, plan.checkpoint.root);
	ASSERT_EQ (1, plan.checkpoint.account_count);
	// Keep the top 5, drop the rest; kept + droppable accounts for the whole chain.
	ASSERT_EQ (5, plan.kept_blocks);
	ASSERT_EQ (total_height - 5, plan.droppable_blocks);
	ASSERT_EQ (total_height, plan.kept_blocks + plan.droppable_blocks);
	ASSERT_GT (plan.reclaimable_bytes, 0);
	// Every retained account frontier proves against the checkpoint root.
	ASSERT_EQ (1, plan.accounts_checked);
	ASSERT_EQ (1, plan.accounts_proven);
	ASSERT_TRUE (plan.all_proven);

	// A window taller than the chain drops nothing.
	auto plan_full = nano::plan_capped_retention (ledger, transaction, 1000, 0);
	ASSERT_EQ (0, plan_full.droppable_blocks);
	ASSERT_EQ (total_height, plan_full.kept_blocks);
	ASSERT_EQ (0, plan_full.reclaimable_bytes);
}
