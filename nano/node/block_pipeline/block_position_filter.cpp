#include <nano/node/block_pipeline/block_position_filter.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/store.hpp>

void nano::block_pipeline::block_position_filter::sink (context & context)
{
	auto const & block = context.block;
	auto const & previous = context.previous;
	if (previous == nullptr)
	{
		pass (context);
	}
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
		{
			switch (previous->type ())
			{
				case nano::block_type::state:
					reject (context);
					return;
				default:
					pass (context);
					return;
			}
		}
		default:
			pass (context);
			return;
	}
}
