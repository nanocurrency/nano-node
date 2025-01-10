#include <celerix/lib/blocks.hpp>
#include <celerix/node/block_processor.hpp>
#include <celerix/node/process_live_dispatcher.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/node/vote_cache.hpp>
#include <celerix/node/websocket.hpp>
#include <celerix/secure/common.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/transaction.hpp>
#include <celerix/store/component.hpp>

celerix::process_live_dispatcher::process_live_dispatcher (celerix::ledger & ledger, celerix::scheduler::priority & scheduler, celerix::vote_cache & vote_cache, celerix::websocket_server & websocket) :
	ledger{ ledger },
	scheduler{ scheduler },
	vote_cache{ vote_cache },
	websocket{ websocket }
{
}

void celerix::process_live_dispatcher::connect (celerix::block_processor & block_processor)
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

void celerix::process_live_dispatcher::inspect (celerix::block_status const & result, celerix::block const & block, secure::transaction const & transaction)
{
	switch (result)
	{
		case celerix::block_status::progress:
			process_live (block, transaction);
			break;
		default:
			break;
	}
}

void celerix::process_live_dispatcher::process_live (celerix::block const & block, secure::transaction const & transaction)
{
	if (websocket.server && websocket.server->any_subscriber (celerix::websocket::topic::new_unconfirmed_block))
	{
		websocket.server->broadcast (celerix::websocket::message_builder ().new_block_arrived (block));
	}
}
