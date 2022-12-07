#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
	  This filter checks the restrictions on epoch blocks.
	  Epoch blocks cannot change the state of an account other than changing the account's epoch
	 */
	class epoch_restrictions_filter
	{
	public:
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject_balance;
		std::function<void (context & context)> reject_representative;
	};
}
}
