#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/link_filter.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

namespace
{
class context
{
public:
	context () :
		filter{ nano::dev::constants.epochs }
	{
		filter.account = [this] (nano::block_pipeline::context & context) {
			account.push_back (context);
		};
		filter.hash = [this] (nano::block_pipeline::context & context) {
			hash.push_back (context);
		};
		filter.noop = [this] (nano::block_pipeline::context & context) {
			noop.push_back (context);
		};
		filter.epoch = [this] (nano::block_pipeline::context & context) {
			epoch.push_back (context);
		};
	}
	nano::block_pipeline::link_filter filter;
	std::vector<nano::block_pipeline::context> account;
	std::vector<nano::block_pipeline::context> hash;
	std::vector<nano::block_pipeline::context> noop;
	std::vector<nano::block_pipeline::context> epoch;
};
nano::block_pipeline::context noop_state_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount) // Unchanged balance
				   .link (0) // Noop
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	return result;
}
nano::block_pipeline::context noop_change_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.change () // Change block is a noop
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	return result;
}
nano::block_pipeline::context account_state_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1) // Decreasing balance
				   .link (nano::dev::genesis_key.pub) // Destination account
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	return result;
}
nano::block_pipeline::context account_send_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.send () // Change block is an account
				   .previous (nano::dev::genesis->hash ())
				   .destination (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	return result;
}
nano::block_pipeline::context hash_state_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.previous = builder.send ()
					  .previous (nano::dev::genesis->hash ())
					  .destination (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - 1)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (0)
					  .build_shared ();
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (result.previous->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount) // Increasing balance
				   .link (result.previous->hash ()) // Source block
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context hash_receive_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.previous = builder.send ()
					  .previous (nano::dev::genesis->hash ())
					  .destination (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - 1)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (0)
					  .build_shared ();
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.receive () // Receive block is a hash
				   .previous (nano::dev::genesis->hash ())
				   .source (result.previous->hash ())
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	return result;
}
nano::block_pipeline::context epoch_state_blocks ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount)
				   .link (nano::dev::network_params.ledger.epochs.link (nano::epoch::epoch_1))
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount;
	return result;
}
}

TEST (link_filter, noop_state)
{
	context context;
	auto blocks = noop_state_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.noop.size ());
}

TEST (link_filter, noop_change)
{
	context context;
	auto blocks = noop_change_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.noop.size ());
}

TEST (link_filter, account_state)
{
	context context;
	auto blocks = account_state_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.account.size ());
}

TEST (link_filter, account_send)
{
	context context;
	auto blocks = account_send_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.account.size ());
}

TEST (link_filter, hash_state)
{
	context context;
	auto blocks = hash_state_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.hash.size ());
}

TEST (link_filter, hash_send)
{
	context context;
	auto blocks = hash_receive_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.hash.size ());
}

TEST (link_filter, epoch_state)
{
	context context;
	auto blocks = epoch_state_blocks ();
	context.filter.sink (blocks);
	ASSERT_EQ (1, context.epoch.size ());
}
