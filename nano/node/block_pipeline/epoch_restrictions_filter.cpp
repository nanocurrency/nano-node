#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/epoch_restrictions_filter.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

void nano::block_pipeline::epoch_restrictions_filter::sink (context & context)
{
	debug_assert (context.state.has_value ());
	if (context.state->balance != context.block->balance ())
	{
		reject_balance (context);
		return;
	}
	if (context.state->representative != context.block->representative ())
	{
		reject_representative (context);
		return;
	}
	if (context.block->previous ().is_zero () && !context.any_pending)
	{
		reject_gap_open (context);
		return;
	}
	pass (context);
}
