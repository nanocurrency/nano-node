#include <nano/lib/blocks.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/gap_cache.hpp>
#include <nano/node/gap_tracker.hpp>

nano::gap_tracker::gap_tracker (nano::gap_cache & gap_cache) :
	gap_cache{ gap_cache }
{
}

void nano::gap_tracker::connect (nano::block_processor & block_processor)
{
	block_processor.processed.add ([this] (auto const & result, auto const & block) {
		switch (result.code)
		{
			case nano::process_result::gap_previous:
			case nano::process_result::gap_source:
				observe (block);
				break;
			default:
				break;
		}
	});
}

void nano::gap_tracker::observe (std::shared_ptr<nano::block> block)
{
	gap_cache.add (block->hash ());
}
