#include <nano/node/bootstrap/account_feedback.hpp>
#include <nano/secure/common.hpp>

namespace nano::bootstrap
{
account_feedback classify_feedback (nano::block_status result, nano::block_source source)
{
	switch (result)
	{
		case nano::block_status::progress:
		{
			if (source == nano::block_source::live)
			{
				// Progress blocks from live traffic don't need further bootstrapping
				return account_feedback::none;
			}
			else
			{
				// Any other block that made progress suggests more of its account is waiting to be pulled
				return account_feedback::progressed;
			}
		}
		case nano::block_status::gap_source:
		{
			if (source == nano::block_source::bootstrap)
			{
				// The account cannot be pulled further until its missing source arrives
				return account_feedback::blocked;
			}
			else
			{
				// Prevent malicious live traffic from filling up the blocked set
				return account_feedback::none;
			}
		}
		case nano::block_status::gap_previous:
		{
			if (source == nano::block_source::live)
			{
				// A live block that is ahead of the ledger shows that its account is worth pulling
				return account_feedback::behind;
			}
			else
			{
				// Only live traffic shows that the network is ahead; bootstrap follows up on its own pulls
				return account_feedback::none;
			}
		}
		default: // No need to handle other cases
			return account_feedback::none;
	}
}
}
