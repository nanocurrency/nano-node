#include <nano/store/account.hpp>

std::optional<nano::account_info> nano::account_store::get (nano::transaction const & transaction, nano::account const & account)
{
	nano::account_info info;
	bool error = get (transaction, account, info);
	if (!error)
	{
		return info;
	}
	else
	{
		return std::nullopt;
	}
}
