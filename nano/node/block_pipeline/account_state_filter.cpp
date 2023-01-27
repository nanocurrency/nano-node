#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/account_state_filter.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

nano::block_pipeline::account_state_filter::account_state_filter (nano::ledger & ledger) :
	ledger{ ledger }
{
}

void nano::block_pipeline::account_state_filter::sink (context & context) const
{
	{
		auto transaction = ledger.store.tx_begin_read ();
		context.previous = ledger.store.block.get (transaction, context.block->previous ());
		if (!context.gap_previous ())
		{
			context.state = ledger.account_info (transaction, context.account ());
			if (!context.state)
			{
				context.state = nano::account_info{};
			}
			context.pending = ledger.pending_info (transaction, { context.account (), context.source () });
		}
		if (ledger.store.block.exists (transaction, context.block->hash ()))
		{
			context.block = nullptr; // Signal this block already exists by nulling out block
		}
	}
	if (context.block == nullptr)
	{
		reject_existing (context);
	}
	else if (context.gap_previous ())
	{
		reject_gap (context);
	}
	else
	{
		pass (context);
	}
}
