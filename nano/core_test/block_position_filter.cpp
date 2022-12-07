#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/block_position_filter.hpp>
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
	context ()
	{
		filter.pass = [this] (nano::block_pipeline::context & context) {
			pass.push_back (std::make_pair (context.block, context.previous));
		};
		filter.reject = [this] (nano::block_pipeline::context & context) {
			reject.push_back (context.block);
		};
	}
	nano::block_pipeline::block_position_filter filter;
	std::vector<std::pair<std::shared_ptr<nano::block>, std::shared_ptr<nano::block>>> pass;
	std::vector<std::shared_ptr<nano::block>> reject;
};
nano::work_pool pool{ nano::dev::network_params.network, 1 };
nano::block_pipeline::context pass_block ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (nano::dev::genesis_key.pub)
				   .previous (nano::dev::genesis->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .balance (nano::dev::constants.genesis_amount - 1)
				   .link (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();
	result.previous = nano::dev::genesis;
	return result;
}
nano::block_pipeline::context reject_block ()
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
	result.block = builder.change ()
				   .previous (result.previous->hash ())
				   .representative (nano::dev::genesis_key.pub)
				   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				   .work (0)
				   .build_shared ();

	return result;
}
}

TEST (block_position_filter, pass)
{
	context context;
	auto pass = pass_block ();
	context.filter.sink (pass);
	ASSERT_EQ (0, context.reject.size ());
	ASSERT_EQ (1, context.pass.size ());
	ASSERT_EQ (pass.block, context.pass[0].first);
}

TEST (block_position_filter, reject)
{
	context context;
	auto reject = reject_block ();
	context.filter.sink (reject);
	ASSERT_EQ (1, context.reject.size ());
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (reject.block, context.reject[0]);
}
