#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
	 This class filters blocks that don't follow restrictions on receiving.
	 Receiving must:
	   - Receive a block that has not been received already
	   - Update the balance to the sum of the previous balance plus the amount received
 */
	class receive_restrictions_filter
	{
	public:
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject_balance;
		std::function<void (context & context)> reject_pending;
	};
} // namespace block_pipeline
} // namespacenano
