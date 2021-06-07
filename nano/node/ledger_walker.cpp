#include <nano/lib/blocks.hpp>
#include <nano/lib/errors.hpp>
#include <nano/node/ledger_walker.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/ledger.hpp>

#include <utility>

nano::ledger_walker::ledger_walker (nano::ledger const & ledger_a) :
	ledger{ ledger_a },
	walked_blocks{},
	blocks_to_walk{}
{
	if (ledger.store.init_error ())
	{
		throw nano::error{ "Ledger store initialization error" };
	}
}

void nano::ledger_walker::walk_backward (nano::block_hash const & start_block_a, visitor_callback const & visitor_callback_a)
{
	const auto transaction = ledger.store.tx_begin_read ();

	enqueue_block (start_block_a);
	while (!blocks_to_walk.empty ())
	{
		const auto block = dequeue_block (transaction);
		if (!visitor_callback_a (block))
		{
			continue;
		}

		if (!block->previous ().is_zero ())
		{
			enqueue_block (ledger.store.block_get (transaction, block->previous ()));
		}

		if (block->sideband ().details.is_receive)
		{
			enqueue_block (ledger.store.block_get (transaction, block->link ().as_block_hash ()));
		}
	}

	walked_blocks.clear ();
	decltype (blocks_to_walk){}.swap (blocks_to_walk);
}

void nano::ledger_walker::enqueue_block (nano::block_hash block_a)
{
	if (walked_blocks.emplace (block_a).second)
	{
		blocks_to_walk.emplace (std::move (block_a));
	}
}

void nano::ledger_walker::enqueue_block (std::shared_ptr<nano::block> const & block_a)
{
	if (block_a)
	{
		enqueue_block (block_a->hash ());
	}
}

std::shared_ptr<nano::block> nano::ledger_walker::dequeue_block (nano::transaction const & transaction_a)
{
	auto block = ledger.store.block_get (transaction_a, blocks_to_walk.top ());
	blocks_to_walk.pop ();

	return block;
}
