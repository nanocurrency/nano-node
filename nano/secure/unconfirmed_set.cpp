#include <nano/lib/blocks.hpp>
#include <nano/secure/unconfirmed_set.hpp>

bool nano::unconfirmed_set::receivable_any (nano::account const & account) const
{
	nano::pending_key begin{ account, 0 };
	nano::pending_key end{ account.number () + 1, 0 };
	return receivable.lower_bound (begin) != receivable.lower_bound (end);
}

void nano::unconfirmed_set::weight_add (nano::account const & account, nano::amount const & amount, nano::amount const & base)
{
	auto existing = weight.find (account);
	if (existing != weight.end ())
	{
		auto new_val = existing->second.number () + amount.number ();
		if (new_val != base.number ())
		{
			existing->second = new_val;
		}
		else
		{
			weight.erase (existing);
		}
	}
	else
	{
		weight[account] = base.number () + amount.number ();
	}
}
