#include <celerix/store/confirmation_height.hpp>
#include <celerix/store/typed_iterator_templ.hpp>

template class celerix::store::typed_iterator<celerix::account, celerix::confirmation_height_info>;

std::optional<celerix::confirmation_height_info> celerix::store::confirmation_height::get (store::transaction const & transaction, celerix::account const & account)
{
	celerix::confirmation_height_info info;
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
