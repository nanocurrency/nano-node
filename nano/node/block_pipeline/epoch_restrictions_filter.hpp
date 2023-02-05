#pragma once

#include <functional>

namespace nano
{
class ledger;
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
		epoch_restrictions_filter (nano::ledger & ledger);
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject_balance;
		std::function<void (context & context)> reject_representative;
		std::function<void (context & context)> reject_gap_open;

	private:
		nano::ledger & ledger;
	};
}
}
