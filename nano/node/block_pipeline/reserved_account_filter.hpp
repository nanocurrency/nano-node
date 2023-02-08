#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
	Filters accounts that are reserved and cannot be used e.g. the account number 0.
 */
	class reserved_account_filter
	{
	public:
		void sink (context & context) const;
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject;
	};
} // namespace pipeline
} // namespace nano
