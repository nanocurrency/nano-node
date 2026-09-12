#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/storage_weighted_work.hpp>
#include <nano/test_common/ledger_context.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
std::shared_ptr<nano::block> send_to (nano::account const & destination)
{
	nano::block_builder builder;
	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };
	return builder.state ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (destination)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (*pool.generate (nano::dev::genesis->hash ()))
	.build ();
}
}

// A send to a not-yet-opened account is storage-weighted: its required work is the base
// requirement scaled by the multiplier, and at multiplier 1 it collapses to the base.
TEST (storage_weighted_work, new_account_is_weighted)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	nano::keypair fresh;
	auto block = send_to (fresh.pub);
	auto transaction = ledger.tx_begin_read ();

	ASSERT_TRUE (nano::block_adds_new_account (ledger, transaction, *block));

	uint64_t const base = ledger.work.threshold_base (block->work_version ());
	auto weighted = nano::evaluate_storage_weighted_work (ledger, transaction, *block, 8.0);
	ASSERT_TRUE (weighted.creates_new_account);
	ASSERT_EQ (base, weighted.base_threshold);
	ASSERT_EQ (nano::difficulty::from_multiplier (8.0, base), weighted.required_threshold);
	ASSERT_GT (weighted.required_threshold, weighted.base_threshold);

	// Multiplier 1 must not raise the requirement, and the block's own work satisfies it.
	auto unweighted = nano::evaluate_storage_weighted_work (ledger, transaction, *block, 1.0);
	ASSERT_EQ (base, unweighted.required_threshold);
	ASSERT_TRUE (unweighted.satisfies);
}

// A send to an already-opened account carries no storage weight, whatever the multiplier.
TEST (storage_weighted_work, existing_account_not_weighted)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	// Genesis account already exists in the ledger.
	auto block = send_to (nano::dev::genesis_key.pub);
	auto transaction = ledger.tx_begin_read ();

	ASSERT_FALSE (nano::block_adds_new_account (ledger, transaction, *block));

	auto weighted = nano::evaluate_storage_weighted_work (ledger, transaction, *block, 8.0);
	ASSERT_FALSE (weighted.creates_new_account);
	ASSERT_EQ (weighted.base_threshold, weighted.required_threshold);
	ASSERT_DOUBLE_EQ (1.0, weighted.weight_multiplier);
	ASSERT_TRUE (weighted.satisfies);
}

// A multiplier below 1 can never lower the base requirement.
TEST (storage_weighted_work, multiplier_floor)
{
	auto ctx = nano::test::ledger_empty ();
	auto & ledger = ctx.ledger ();
	nano::keypair fresh;
	auto block = send_to (fresh.pub);
	auto transaction = ledger.tx_begin_read ();

	auto result = nano::evaluate_storage_weighted_work (ledger, transaction, *block, 0.25);
	ASSERT_EQ (result.base_threshold, result.required_threshold);
	ASSERT_DOUBLE_EQ (1.0, result.weight_multiplier);
}
