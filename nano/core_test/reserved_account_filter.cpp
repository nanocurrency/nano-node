#include <nano/lib/blockbuilders.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/reserved_account_filter.hpp>

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
	nano::block_pipeline::reserved_account_filter filter;
	std::vector<nano::block_pipeline::context> pass;
	std::vector<nano::block_pipeline::context> reject;
};
nano::block_pipeline::context reject_open ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.open ()
				   .source (0)
				   .representative (0)
				   .account (0)
				   .sign (0, 0)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context reject_state ()
{
	nano::block_builder builder;
	nano::block_pipeline::context result;
	result.block = builder.state ()
				   .account (0)
				   .previous (0)
				   .representative (0)
				   .balance (0)
				   .link (0)
				   .sign (0, 0)
				   .work (0)
				   .build_shared ();
	return result;
}
nano::block_pipeline::context pass ()
{
	nano::block_pipeline::context result;
	result.block = nano::dev::genesis;
	return result;
}
}

TEST (reserved_account_filter, pass)
{
	context context;
	auto block = pass ();
	context.filter.sink (block);
	ASSERT_EQ (1, context.pass.size ());
	ASSERT_EQ (block.block, context.pass[0].block);
	ASSERT_EQ (0, context.reject.size ());
}

TEST (reserved_account_filter, reject_open)
{
	context context;
	auto block = reject_open ();
	context.filter.sink (block);
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (1, context.reject.size ());
	ASSERT_EQ (block.block, context.reject[0].block);
}

TEST (reserved_account_filter, reject_state)
{
	context context;
	auto block = reject_state ();
	context.filter.sink (block);
	ASSERT_EQ (0, context.pass.size ());
	ASSERT_EQ (1, context.reject.size ());
	ASSERT_EQ (block.block, context.reject[0].block);
}
