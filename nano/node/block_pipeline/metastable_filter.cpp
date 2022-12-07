#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/metastable_filter.hpp>

void nano::block_pipeline::metastable_filter::sink (context & context)
{
	debug_assert (context.state.has_value ());
	if (context.block->previous () == context.state->head)
	{
		pass (context);
	}
	else
	{
		reject (context);
	}
}
