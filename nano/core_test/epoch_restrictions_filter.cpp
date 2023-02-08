#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/epoch_restrictions_filter.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>
#include <nano/secure/utility.hpp>

#include <gtest/gtest.h>

namespace
{
class context
{
public:
	context ()
	{
		filter.pass = [this] (nano::block_pipeline::context & context) {
			pass.push_back (context);
		};
		filter.reject_balance = [this] (nano::block_pipeline::context & context) {
			reject_balance.push_back (context);
		};
		filter.reject_representative = [this] (nano::block_pipeline::context & context) {
			reject_representative.push_back (context);
		};
		filter.reject_gap_open = [this] (nano::block_pipeline::context & context) {
			reject_gap_open.push_back (context);
		};
	}
	nano::block_pipeline::epoch_restrictions_filter filter;
	std::vector<nano::block_pipeline::context> pass;
	std::vector<nano::block_pipeline::context> reject_balance;
	std::vector<nano::block_pipeline::context> reject_representative;
	std::vector<nano::block_pipeline::context> reject_gap_open;
};
nano::block_pipeline::context epoch_pass_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount) // Unchanged balance
				   .link (nano::dev::constants.epochs.link (nano::epoch::epoch_1))
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	result.state->representative = nano::dev::genesis_key.pub;
	return result;
}
nano::block_pipeline::context epoch_reject_balance_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub) // Unchacked representative
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (nano::dev::constants.epochs.link (nano::epoch::epoch_1))
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount; // Changed balance
	result.state->representative = nano::dev::genesis_key.pub;
	return result;
}
nano::block_pipeline::context epoch_reject_representative_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount) // Unchanged balance
				   .link (nano::dev::constants.epochs.link (nano::epoch::epoch_1))
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	nano::keypair dummy;
	result.state->representative = dummy.pub; // Changed representative
	return result;
}
nano::block_pipeline::context epoch_reject_gap_open_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	nano::keypair dummy;
	result.block = builder.state ()
				   .account (dummy.pub)
				   .previous (0) // First block
				   .representative (0)
				   .balance (0)
				   .link (nano::dev::constants.epochs.link (nano::epoch::epoch_1))
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	return result;
}
}

TEST (epoch_restrictions_filter, epoch_pass)
{
	context context;
	auto blocks = epoch_pass_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (epoch_restrictions_filter, epoch_reject_balance)
{
	context context;
	auto blocks = epoch_reject_balance_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.reject_balance.size ());
}

TEST (epoch_restrictions_filter, epoch_reject_representative)
{
	context context;
	auto blocks = epoch_reject_representative_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.reject_representative.size ());
}

TEST (epoch_restrictions_filter, epoch_reject_gap_open)
{
	context context;
	auto blocks = epoch_reject_gap_open_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.reject_gap_open.size ());
}
