#include <nano/store/account.hpp>
#include <nano/store/reverse_iterator_templ.hpp>
#include <nano/store/typed_iterator_templ.hpp>

template class nano::store::typed_iterator<nano::account, nano::account_info>;
template class nano::store::reverse_iterator<nano::store::typed_iterator<nano::account, nano::account_info>>;

std::optional<nano::account_info> nano::store::account::get (store::transaction const & transaction, nano::account const & account)
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

auto nano::store::account::rbegin (store::transaction const & tx) const -> reverse_iterator
{
	auto iter = end (tx);
	--iter;
	return reverse_iterator{ std::move (iter) };
}

auto nano::store::account::rend (transaction const & tx) const -> reverse_iterator
{
	return reverse_iterator{ end (tx) };
}
