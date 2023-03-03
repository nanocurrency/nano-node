#include <nano/node/block_arrival.hpp>
#include <nano/node/block_broadcast.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/network.hpp>

nano::block_broadcast::block_broadcast (nano::network & network, nano::block_arrival & block_arrival) :
	network{ network },
	block_arrival{ block_arrival }
{
}

void nano::block_broadcast::connect (nano::block_processor & block_processor, bool enabled)
{
	if (!enabled)
	{
		return;
	}
	block_processor.processed.add ([this] (auto const & result, auto const & block) {
		switch (result.code)
		{
			case nano::process_result::progress:
				observe (block);
				break;
			default:
				break;
		}
		erase (block);
	});
}

void nano::block_broadcast::observe (std::shared_ptr<nano::block> block)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	auto existing = local.find (block);
	auto local_l = existing != local.end ();
	lock.unlock ();
	if (local_l)
	{
		// Block created on this node
		// Perform more agressive initial flooding
		network.flood_block_initial (block);
	}
	else
	{
		if (block_arrival.recent (block->hash ()))
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

void nano::block_broadcast::set_local (std::shared_ptr<nano::block> block)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	local.insert (block);
}

void nano::block_broadcast::erase (std::shared_ptr<nano::block> block)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	local.erase (block);
}
