#include <nano/lib/blocks.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/election_scheduler.hpp>
#include <nano/node/process_live_dispatcher.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/websocket.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

nano::process_live_dispatcher::process_live_dispatcher (nano::ledger & ledger, nano::election_scheduler & scheduler, nano::vote_cache & inactive_vote_cache, nano::websocket_server & websocket) :
	ledger{ ledger },
	scheduler{ scheduler },
	inactive_vote_cache{ inactive_vote_cache },
	websocket{ websocket }
{
}

void nano::process_live_dispatcher::connect (nano::block_processor & block_processor)
{
	block_processor.batch_processed.add ([this] (auto const & batch) {
		auto const transaction = ledger.store.tx_begin_read ();
		for (auto const & [result, block] : batch)
		{
			debug_assert (block != nullptr);
			inspect (result, *block, transaction);
		}
	});
}

void nano::process_live_dispatcher::inspect (nano::process_return const & result, nano::block const & block, nano::transaction const & transaction)
{
	switch (result.code)
	{
		case nano::process_result::progress:
			process_live (block, transaction);
			break;
		default:
			break;
	}
}

void nano::process_live_dispatcher::process_live (nano::block const & block, nano::transaction const & transaction)
{
	// Start collecting quorum on block
	if (ledger.dependents_confirmed (transaction, block))
	{
		auto account = block.account ().is_zero () ? block.sideband ().account : block.account ();
		scheduler.activate (account, transaction);
	}

	// Notify inactive vote cache about a new live block
	inactive_vote_cache.trigger (block.hash ());

	if (websocket.server && websocket.server->any_subscriber (nano::websocket::topic::new_unconfirmed_block))
	{
		websocket.server->broadcast (nano::websocket::message_builder ().new_block_arrived (block));
	}
}
