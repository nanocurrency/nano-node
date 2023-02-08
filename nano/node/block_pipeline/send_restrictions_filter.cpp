#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/send_restrictions_filter.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

void nano::block_pipeline::send_restrictions_filter::sink (context & context)
{
	debug_assert (context.block->type () == nano::block_type::send || context.block->type () == nano::block_type::state);
	if (context.state->balance < context.block->balance ())
	{
		reject (context);
	}
	pass (context);
}
