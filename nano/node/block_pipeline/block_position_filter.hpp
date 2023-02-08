#pragma once

#include <functional>

namespace nano
{
namespace block_pipeline
{
	class context;
	/**
 * This class filters blocks based on whether their block position is correct
 *
 * The block order concept is to ensure an account's epoch cannot go backwards
 *
 * This implementation compares a block to it's previous block and passes or rejects the block based on whether the epoch goes backwards
 * Previous is passed in but required to be in the ledger
 */
	class block_position_filter
	{
	public:
		void sink (context & context);
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject;
	};
} // namespace block_pipeline
} // namespacenano
