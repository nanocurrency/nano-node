#include <nano/store/confirmation_height.hpp>
#include <nano/store/typed_iterator_templ.hpp>

template class nano::store::typed_iterator<nano::account, nano::confirmation_height_info>;

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
