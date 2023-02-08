#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/link_filter.hpp>

nano::block_pipeline::link_filter::link_filter (nano::epochs & epochs) :
	epochs{ epochs }
{
}

void nano::block_pipeline::link_filter::sink (context & context)
{
	debug_assert (context.state.has_value ());
	switch (context.block->type ())
	{
		case nano::block_type::state:
			if (context.block->balance () < context.state->balance)
			{
				account (context);
				break;
			}
			if (context.block->link ().is_zero ())
			{
				noop (context);
				break;
			}
			if (epochs.is_epoch_link (context.block->link ()))
			{
				epoch (context);
				break;
			}
			hash (context);
			break;
		case nano::block_type::send:
			account (context);
			break;
		case nano::block_type::open:
		case nano::block_type::receive:
			hash (context);
			break;
		case nano::block_type::change:
			noop (context);
			break;
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			debug_assert (false);
			break;
	}
}
