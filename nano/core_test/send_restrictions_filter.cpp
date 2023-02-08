#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/send_restrictions_filter.hpp>
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
		filter.reject = [this] (nano::block_pipeline::context & context) {
			reject.push_back (context);
		};
	}
	nano::block_pipeline::send_restrictions_filter filter;
	std::vector<nano::block_pipeline::context> pass;
	std::vector<nano::block_pipeline::context> reject;
};
nano::block_pipeline::context pass_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount; // Genesis amount in account
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				   .link (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context pass_send_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount; // Genesis amount in account
	result.block = builder.send ()
				   .previous (nano::dev::genesis->hash ())
				   .destination (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.state () // Dummy block
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.link (nano::dev::genesis_key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount) // Balance has increased
				   .link (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_send_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto send = builder.send () // Dummy block
				.previous (nano::dev::genesis->hash ())
				.balance (nano::dev::constants.genesis_amount - 1) // 1 raw nano is sent
				.destination (nano::dev::genesis_key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build_shared ();
	result.state = nano::account_info{};
	result.state->balance = nano::dev::constants.genesis_amount - 1;
	result.block = builder.send ()
				   .previous (nano::dev::genesis->hash ())
				   .balance (nano::dev::constants.genesis_amount) // Balance has increased
				   .destination (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
}

TEST (send_restrictions_filter, pass_send)
{
	context context;
	auto pass = pass_send_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (send_restrictions_filter, pass_state)
{
	context context;
	auto pass = pass_state_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (send_restrictions_filter, reject_send)
{
	context context;
	auto pass = reject_send_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.reject.size ());
}

TEST (send_restrictions_filter, reject_state)
{
	context context;
	auto pass = reject_send_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.reject.size ());
}
