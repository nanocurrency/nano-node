#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
	 This class filters blocks that don't follow restrictions on sending.
	 Sending must not increase its balance
 */
	class send_restrictions_filter
	{
	public:
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject;
	};
} // namespace block_pipeline
} // namespacenano
