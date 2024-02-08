#include <nano/node/block_broadcast.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/network.hpp>

nano::block_broadcast::block_broadcast (nano::network & network, bool enabled) :
	network{ network },
	enabled{ enabled }
{
}

void nano::block_broadcast::connect (nano::block_processor & block_processor)
{
	if (!enabled)
	{
		return;
	}
	block_processor.processed.add ([this] (auto const & result, auto const & block, auto const & context) {
		switch (result.code)
		{
			case nano::process_result::progress:
				observe (block, context);
				break;
			default:
				break;
		}
	});
}

void nano::block_broadcast::observe (std::shared_ptr<nano::block> const & block, nano::block_processor::context const & context)
{
	if (context.source == nano::block_processor::block_source::local)
	{
		// Block created on this node
		// Perform more agressive initial flooding
		network.flood_block_initial (block);
	}
	else
	{
		if (context.source != nano::block_processor::block_source::bootstrap)
		{
			// Block arrived from realtime traffic, do normal gossip.
			network.flood_block (block, nano::transport::buffer_drop_policy::limiter);
		}
		else
		{
			// Block arrived from bootstrap
			// Don't broadcast blocks we're bootstrapping
		}
	}
}
