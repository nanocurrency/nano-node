#pragma once

#include <functional>

namespace nano
{
class ledger;
namespace block_pipeline
{
	class context;
	/**
	  This class populates `context' with the current state of its associated account from data in the ledger.
	  This information is used for subsequent pipeline stages to filter and process blocks
 */
	class account_state_filter
	{
	public:
		account_state_filter (nano::ledger & ledger);
		// The `context' passed in must have the `previous' field set correctly.
		void sink (context & context) const;
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject_gap;
		std::function<void (context & context)> reject_existing;

	private:
		nano::ledger & ledger;
	};
} // namespace pipeline
} // namespace nano
