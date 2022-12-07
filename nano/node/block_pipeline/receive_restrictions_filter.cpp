#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/receive_restrictions_filter.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

void nano::block_pipeline::receive_restrictions_filter::sink (context & context)
{
	debug_assert (context.state.has_value ());
	if (!context.pending.has_value ())
	{
		reject_pending (context);
		return;
	}
	if (context.block->type () == nano::block_type::state)
	{
		auto next_balance = context.state->balance.number () + context.pending->amount.number ();
		if (next_balance != context.block->balance ().number ())
		{
			reject_balance (context);
			return;
		}
	}
	pass (context);
}
