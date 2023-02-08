#pragma once

#include <functional>

namespace nano
{
class epochs;
namespace block_pipeline
{
	class context;
	/**
	  This class filters blocks in four directions based on how the link field should be interpreted
	  For state blocks the link field is interpreted as:
	    If the balance has decreased, a destination account
	    If the balance has not decreased
	      If the link field is 0, a noop
	      If the link field is an epoch link, an epoch sentinel
	      Otherwise, a block hash of an block ready to be received
	  For legacy blocks, the link field interpretation is applied to source field for receive and open blocks or the destination field for send blocks
 */
	class link_filter
	{
	public:
		link_filter (nano::epochs & epochs);
		void sink (context & context);
		std::function<void (context & context)> hash;
		std::function<void (context & context)> account;
		std::function<void (context & context)> noop;
		std::function<void (context & context)> epoch;

	private:
		nano::epochs & epochs;
	};
} // namespace block_pipeline
} // namespacenano
