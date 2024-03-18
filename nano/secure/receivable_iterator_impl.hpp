#include <nano/secure/pending_info.hpp>
#include <nano/secure/receivable_iterator.hpp>

template <typename T>
nano::receivable_iterator<T>::receivable_iterator ()
{
}

template <typename T>
nano::receivable_iterator<T>::receivable_iterator (store::transaction const & transaction, T const & view, std::optional<std::pair<nano::pending_key, nano::pending_info>> const & item) :
	transaction{ &transaction },
	view{ &view },
	item{ item }
{
	if (item.has_value ())
	{
		account = item.value ().first.account;
	}
}

template <typename T>
bool nano::receivable_iterator<T>::operator== (receivable_iterator const & other) const
{
	debug_assert (view == nullptr || other.view == nullptr || view == other.view);
	debug_assert (account.is_zero () || other.account.is_zero () || account == other.account);
	return item == other.item;
}

template <typename T>
bool nano::receivable_iterator<T>::operator!= (receivable_iterator const & other) const
{
	return !(*this == other);
}

template <typename T>
auto nano::receivable_iterator<T>::operator++ () -> receivable_iterator<T> &
{
	item = view->receivable_lower_bound (*transaction, item.value ().first.account, item.value ().first.hash.number () + 1);
	if (item && item.value ().first.account != account)
	{
		item = std::nullopt;
	}
	return *this;
}

template <typename T>
std::pair<nano::pending_key, nano::pending_info> const & nano::receivable_iterator<T>::operator* () const
{
	return item.value ();
}

template <typename T>
std::pair<nano::pending_key, nano::pending_info> const * nano::receivable_iterator<T>::operator->() const
{
	return &item.value ();
}
