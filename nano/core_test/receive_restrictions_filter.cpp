#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/receive_restrictions_filter.hpp>
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
			pass.push_back (std::make_pair (context.block, context.previous));
		};
		filter.reject_balance = [this] (nano::block_pipeline::context & context) {
			reject_balance.push_back (context.block);
		};
		filter.reject_pending = [this] (nano::block_pipeline::context & context) {
			reject_pending.push_back (context.block);
		};
	}
	nano::block_pipeline::receive_restrictions_filter filter;
	std::vector<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<nano::block>>> pass;
	std::vector<std::shared_ptr<nano::block>> reject_balance;
	std::vector<std::shared_ptr<nano::block>> reject_pending;
};
nano::block_pipeline::context pass_receive_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.pending = nano::pending_info{ nano::dev::genesis_key.pub, 1 /* 1 raw nano is receivable */, nano::epoch::epoch_0 };
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context pass_open_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	nano::keypair key;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.pending = nano::pending_info{ nano::dev::genesis_key.pub, 1 /* 1 raw nano is receivable */, nano::epoch::epoch_0 };
	result.state = nano::account_info{};
	result.state->balance = 0;
	result.block = builder.open ()
				   .source (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .account (key.pub)
				   .sign (key.prv, key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context pass_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.pending = nano::pending_info{ nano::dev::genesis_key.pub, 1 /* 1 raw nano is receivable */, nano::epoch::epoch_0 };
	result.state = nano::account_info{};
	result.state->balance = 0;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (1)
				   .link (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_pending_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	// result.pending is not set, there is no pending entry
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (1)
				   .link (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_pending_receive_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	// result.pending is not set, there is no pending entry
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.receive ()
				   .previous (send->hash ())
				   .source (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_balance_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.destination (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.pending = nano::pending_info{ nano::dev::genesis_key.pub, 1 /* 1 raw nano is receivable */, nano::epoch::epoch_0 };
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (send->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (2) // Balance does not match how much was sent
				   .link (send->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
}

TEST (receive_restrictions_filter, pass_receive)
{
	context context;
	auto pass = pass_receive_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (receive_restrictions_filter, pass_open)
{
	context context;
	auto pass = pass_open_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (receive_restrictions_filter, pass_state)
{
	context context;
	auto pass = pass_state_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (receive_restrictions_filter, reject_pending_state)
{
	context context;
	auto reject = reject_pending_state_block ();
	context.filter.sink (reject);
	ASSERT_EQ (1, context.reject_pending.size ());
}

TEST (receive_restrictions_filter, reject_pending_receive)
{
	context context;
	auto reject = reject_pending_receive_block ();
	context.filter.sink (reject);
	ASSERT_EQ (1, context.reject_pending.size ());
}

TEST (receive_restrictions_filter, reject_balance)
{
	context context;
	auto reject = reject_balance_block ();
	context.filter.sink (reject);
	ASSERT_EQ (1, context.reject_balance.size ());
}
