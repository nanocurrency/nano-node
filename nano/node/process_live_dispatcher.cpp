#include <nano/lib/blocks.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/process_live_dispatcher.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/websocket.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/transaction.hpp>
#include <nano/store/component.hpp>

nano::process_live_dispatcher::process_live_dispatcher (nano::ledger & ledger, nano::scheduler::priority & scheduler, nano::vote_cache & vote_cache, nano::websocket_server & websocket) :
	ledger{ ledger },
	scheduler{ scheduler },
	vote_cache{ vote_cache },
	websocket{ websocket }
{
}

void nano::process_live_dispatcher::connect (nano::block_processor & block_processor)
{
	block_processor.batch_processed.add ([this] (auto const & batch) {
		auto const transaction = ledger.tx_begin_read ();
		for (auto const & [result, context] : batch)
		{
			debug_assert (context.block != nullptr);
			inspect (result, *context.block, transaction);
		}
	});
}

void nano::process_live_dispatcher::inspect (nano::block_status const & result, nano::block const & block, secure::transaction const & transaction)
{
	switch (result)
	{
		case nano::block_status::progress:
			process_live (block, transaction);
			break;
		default:
			break;
	}
}

void nano::process_live_dispatcher::process_live (nano::block const & block, secure::transaction const & transaction)
{
	// Start collecting quorum on block
	if (ledger.dependents_confirmed (transaction, block))
	{
		scheduler.activate (block.account ());
	}

	if (websocket.server && websocket.server->any_subscriber (nano::websocket::topic::new_unconfirmed_block))
	{
		websocket.server->broadcast (nano::websocket::message_builder ().new_block_arrived (block));
	}
}
