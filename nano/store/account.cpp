#include <celerix/store/account.hpp>
#include <celerix/store/reverse_iterator_templ.hpp>
#include <celerix/store/typed_iterator_templ.hpp>

template class celerix::store::typed_iterator<celerix::account, celerix::account_info>;
template class celerix::store::reverse_iterator<celerix::store::typed_iterator<celerix::account, celerix::account_info>>;

std::optional<celerix::account_info> celerix::store::account::get (store::transaction const & transaction, celerix::account const & account)
{
	celerix::account_info info;
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

auto celerix::store::account::rbegin (store::transaction const & tx) const -> reverse_iterator
{
	auto iter = end (tx);
	--iter;
	return reverse_iterator{ std::move (iter) };
}

auto celerix::store::account::rend (transaction const & tx) const -> reverse_iterator
{
	return reverse_iterator{ end (tx) };
}
