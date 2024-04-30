#include <nano/secure/account_info.hpp>
#include <nano/secure/account_iterator.hpp>

template <typename Set>
nano::account_iterator<Set>::account_iterator ()
{
}

template <typename Set>
nano::account_iterator<Set>::account_iterator (secure::transaction const & transaction, Set const & set, std::optional<std::pair<nano::account, nano::account_info>> const & item) :
	transaction{ &transaction },
	set{ &set },
	item{ item }
{
}

template <typename Set>
bool nano::account_iterator<Set>::operator== (account_iterator const & other) const
{
	debug_assert (set == nullptr || other.set == nullptr || set == other.set);
	return item == other.item;
}

// Iteration is performed by calling set->account_lower_bound (tx, next) where next is one higher than the current iterator
template <typename Set>
auto nano::account_iterator<Set>::operator++ () -> account_iterator<Set> &
{
	auto next = item.value ().first.number () + 1;
	if (next != 0)
	{
		*this = set->account_lower_bound (*transaction, next);
	}
	else
	{
		// Convert to and end iterator if there are no more items
		*this = account_iterator<Set>{};
	}
	return *this;
}

template <typename Set>
std::pair<nano::account, nano::account_info> const & nano::account_iterator<Set>::operator* () const
{
	return item.value ();
}

template <typename Set>
std::pair<nano::account, nano::account_info> const * nano::account_iterator<Set>::operator->() const
{
	return &item.value ();
}
