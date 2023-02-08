#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/determinate_signer_filter.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

nano::block_pipeline::determinate_signer_filter::determinate_signer_filter (nano::ledger & ledger) :
	ledger{ ledger }
{
}

void nano::block_pipeline::determinate_signer_filter::sink (context & context) const
{
	auto & block = context.block;
	auto & previous = context.previous;
	auto & account = context.account;
	debug_assert (block != nullptr);
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
			debug_assert (previous != nullptr); // Previous block must be passed in for non-open blocks
			switch (previous->type ())
			{
				case nano::block_type::state:
					debug_assert (false && "Legacy blocks can't follow state blocks");
					break;
				case nano::block_type::open:
					// Open blocks have the account written in the block.
					account = previous->account ();
					break;
				default:
					// Other legacy block types have the account stored in sideband.
					account = previous->sideband ().account;
					break;
			}
			break;
		case nano::block_type::state:
		{
			debug_assert (ledger.is_epoch_link (block->link ())); // Non-epoch state block signer is determined statelessly as it's written in the block
			debug_assert (dynamic_cast<nano::state_block *> (block.get ()));
			auto transaction = ledger.store.tx_begin_read ();
			auto is_send = ledger.is_send (transaction, *static_cast<nano::state_block *> (block.get ()));
			// If the block is a send, while the link field may contain an epoch link value, it is actually a malformed destination address.
			account = is_send ? block->account () : ledger.constants.epochs.signer (ledger.constants.epochs.epoch (block->link ()));
			break;
		}
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
		case nano::block_type::open: // Open block signer is determined statelessly as it's written in the block
			debug_assert (false);
			break;
	}
	debug_assert (!account.is_zero ());
	pass (context);
}
