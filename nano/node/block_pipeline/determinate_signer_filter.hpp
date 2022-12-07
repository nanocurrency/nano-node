#pragma once

#include <functional>
#include <memory>

namespace nano
{
class ledger;
namespace block_pipeline
{
	class context;
	/**
 * This class filters blocks by when their signatures can be verified. Legacy and epoch blocks do not contain all the info required to have their signature checked. One problem is finding the public key to verify the signature, in the case of non-open legacy blocks, the account cannot be easily found without analysing and connecting previous blocks. Another problem is finding the balance before the block being verified, which is needed to determine how to treat the link field, as a block hash or an account number.
 * Blocks can be early which means they can be verified without a ledger access, or they can be late which means the ledger must be accessed to determine the signer or type of block or both.
 */
	class determinate_signer_filter
	{
	public:
		determinate_signer_filter (nano::ledger & ledger);
		void sink (context & context) const;
		std::function<void (context & context)> pass;
		std::function<void (context & context)> reject;

	private:
		nano::ledger & ledger;
	};
} // namespace pipeline
} // namespace nano
