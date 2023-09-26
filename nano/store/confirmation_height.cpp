#include <nano/store/confirmation_height.hpp>

std::optional<nano::confirmation_height_info> nano::store::confirmation_height::get (store::transaction const & transaction, nano::account const & account)
{
	nano::confirmation_height_info info;
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
