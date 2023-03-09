#include <nano/node/active_transactions.hpp>
#include <nano/node/block_publisher.hpp>
#include <nano/node/blockprocessor.hpp>

nano::block_publisher::block_publisher (nano::active_transactions & active) :
	active{ active }
{
}

void nano::block_publisher::connect (nano::block_processor & block_processor)
{
	block_processor.processed.add ([this] (auto const & result, auto const & block) {
		switch (result.code)
		{
			case nano::process_result::fork:
				observe (block);
				break;
			default:
				break;
		}
	});
}

void nano::block_publisher::observe (std::shared_ptr<nano::block> block)
{
	active.publish (block);
}
