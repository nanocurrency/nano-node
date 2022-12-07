#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/account_state_filter.hpp>
#include <nano/node/block_pipeline/context.hpp>
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
	context () :
		store{ nano::make_store (logger, nano::unique_path (), nano::dev::constants) },
		ledger{ *store, stats, nano::dev::constants }
	{
		store->initialize (store->tx_begin_write (), ledger.cache, nano::dev::constants);
		debug_assert (!store->init_error ());
		filter.pass = [this] (nano::block_pipeline::context & context) {
			pass.push_back (context);
		};
		filter.reject_gap = [this] (nano::block_pipeline::context & context) {
			reject_gap.push_back (context);
		};
		filter.reject_existing = [this] (nano::block_pipeline::context & context) {
			reject_existing.push_back (context);
		};
	}
	nano::stat stats;
	nano::logger_mt logger;
	std::shared_ptr<nano::store> store;
	nano::ledger ledger;
	nano::block_pipeline::account_state_filter filter{ ledger };
	nano::keypair signer;
	std::vector<nano::block_pipeline::context> pass;
	std::vector<nano::block_pipeline::context> reject_gap;
	std::vector<nano::block_pipeline::context> reject_existing;
};
nano::work_pool pool{ nano::dev::network_params.network, 1 };
nano::block_pipeline::context previous_open_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	return result;
}
nano::block_pipeline::context previous_send_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.previous = builder.send ()
					  .previous (nano::dev::genesis->hash ())
					  .destination (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - 1)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (nano::dev::genesis->hash ()))
					  .build_shared ();
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (result.previous->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context previous_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.previous = builder.state ()
					  .account (nano::dev::genesis_key.pub)
					  .previous (nano::dev::genesis->hash ())
					  .representative (nano::dev::genesis_key.pub)
					  .balance (nano::dev::constants.genesis_amount - 1)
					  .link (nano::dev::genesis_key.pub)
					  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					  .work (*pool.generate (nano::dev::genesis->hash ()))
					  .build_shared ();
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (result.previous->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_gap_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	auto dummy = builder.state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount)
				 .link (0)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build_shared ();
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (dummy->hash ()) // Previous block is not in ledger
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (0)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_existing_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = nano::dev::genesis;
	return result;
}
}

TEST (account_state_filter, previous_open)
{
	context context;
	auto block = previous_open_block ();
	context.filter.sink (block);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (account_state_filter, previous_send)
{
	context context;
	auto block = previous_send_block ();
	ASSERT_EQ (nano::process_result::progress, context.ledger.process (context.store->tx_begin_write (), *block.previous).code);
	context.filter.sink (block);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (account_state_filter, previous_state)
{
	context context;
	auto block = previous_send_block ();
	ASSERT_EQ (nano::process_result::progress, context.ledger.process (context.store->tx_begin_write (), *block.previous).code);
	context.filter.sink (block);
	ASSERT_EQ (1, context.pass.size ());
}

TEST (account_state_filter, reject_gap)
{
	context context;
	auto block = reject_gap_block ();
	context.filter.sink (block);
	ASSERT_EQ (1, context.reject_gap.size ());
}

TEST (account_state_filter, reject_existing)
{
	context context;
	auto block = reject_existing_block ();
	context.filter.sink (block);
	ASSERT_EQ (1, context.reject_existing.size ());
}
