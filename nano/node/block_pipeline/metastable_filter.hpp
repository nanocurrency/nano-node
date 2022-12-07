#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
	  This filter identifies metastable blocks (forked blocks) with respect to the ledger and rejects them.
	  Rejected blocks need to be resolved via consensus
	  It is assumed that the previous block has already been loaded in to `context' if it exists
	  Fork scenarios are:
	    1) An initial block arriving for an account that's already been initialized
	    2) The previous block exists but it is not the head block
	  Both of these scenarios can be ifentified by checking: if block->previous () == head
	 */
	class metastable_filter
	{
	public:
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject;
	};
}
}
