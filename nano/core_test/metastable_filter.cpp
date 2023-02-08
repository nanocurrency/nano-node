#include <nano/lib/blockbuilders.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/metastable_filter.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

#include <gtest/gtest.h>

namespace
{
class context
{
public:
	context ()
	{
		filter.pass = [this] (nano::block_pipeline::context & context) {
			pass.push_back (context.block);
		};
		filter.reject = [this] (nano::block_pipeline::context & context) {
			reject.push_back (context.block);
		};
	}
	nano::block_pipeline::metastable_filter filter;
	std::vector<std::shared_ptr<nano::block>> pass;
	std::vector<std::shared_ptr<nano::block>> reject;
};
nano::work_pool pool{ nano::dev::network_params.network, 1 };
nano::block_pipeline::context pass_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ()) // Previous block matches current head block
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	result.state = nano::account_info{};
	result.state->head = nano::dev::genesis->hash (); // <- Head block
	return result;
}
nano::block_pipeline::context reject_open_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.open () // Trying to add an open block to an already open account
				   .source (1)
				   .representative (2)
				   .account (3)
				   .sign (4, 5)
				   .work (0)
				   .build_shared ();
	result.state = nano::account_info{};
	result.state->head = 6; // Head block is initialized
	return result;
}
nano::block_pipeline::context reject_initial_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state () // Trying to add an initial state block to an already open account
				   .account (1)
				   .previous (0) // Initial block
				   .representative (2)
				   .balance (3)
				   .link (4)
				   .sign (5, 6)
				   .work (0)
				   .build_shared ();
	result.state = nano::account_info{};
	result.state->head = 6; // Head block is initialized
	return result;
}
nano::block_pipeline::context reject_state_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.previous = builder.state () // For example purposes, construct a dummy previous block we can use to compute a block hash
					  .account (1)
					  .previous (2)
					  .representative (3)
					  .balance (4)
					  .link (5)
					  .sign (6, 7)
					  .work (0)
					  .build_shared ();
	result.block = builder.state ()
				   .account (1)
				   .previous (result.previous->hash ()) // Link this block to the dummy block via its block hash
				   .representative (2)
				   .balance (3)
				   .link (4)
				   .sign (5, 6)
				   .work (0)
				   .build_shared ();
	result.state = nano::account_info{};
	result.state->head = 1; // Assuming precondition that previous exists in the ledger, head block is different therefore it's metastable
	return result;
}
}

TEST (metastable_filter, pass)
{
	context context;
	auto pass = pass_block ();
	context.filter.sink (pass);
	ASSERT_EQ (1, context.pass.size ());
	auto const & block = context.pass[0];
	ASSERT_EQ (pass.block, block);
	ASSERT_EQ (0, context.reject.size ());
}

TEST (metastable_filter, reject_open)
{
	context context;
	auto reject = reject_open_block ();
	context.filter.sink (reject);
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (1, context.reject.size ());
	auto const & block = context.reject[0];
	ASSERT_EQ (reject.block, block);
}

TEST (metastable_filter, reject_initial)
{
	context context;
	auto reject = reject_initial_block ();
	context.filter.sink (reject);
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (1, context.reject.size ());
	auto const & block = context.reject[0];
	ASSERT_EQ (reject.block, block);
}

TEST (metastable_filter, reject_state)
{
	context context;
	auto reject = reject_state_block ();
	context.filter.sink (reject);
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (1, context.reject.size ());
	auto const & block = context.reject[0];
	ASSERT_EQ (reject.block, block);
}
