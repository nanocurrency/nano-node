#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/reserved_account_filter.hpp>

void nano::block_pipeline::reserved_account_filter::sink (context & context) const
{
	switch (context.block->type ())
	{
		case nano::block_type::open:
		case nano::block_type::state:
			if (!context.block->account ().is_zero ())
			{
				pass (context);
			}
			else
			{
				reject (context);
			}
			break;
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			pass (context);
			break;
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			debug_assert (false);
			break;
	}
}
