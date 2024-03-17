#include <nano/secure/pending_info.hpp>
#include <nano/secure/receivable_iterator.hpp>

template <typename Set>
nano::receivable_iterator<Set>::receivable_iterator ()
{
}

template <typename Set>
nano::receivable_iterator<Set>::receivable_iterator (secure::transaction const & transaction, Set const & set, std::optional<std::pair<nano::pending_key, nano::pending_info>> const & item) :
	transaction{ &transaction },
	set{ &set },
	item{ item }
{
	if (item.has_value ())
	{
		account = item.value ().first.account;
	}
}

template <typename Set>
bool nano::receivable_iterator<Set>::operator== (receivable_iterator const & other) const
{
	debug_assert (set == nullptr || other.set == nullptr || set == other.set);
	debug_assert (account.is_zero () || other.account.is_zero () || account == other.account);
	return item == other.item;
}

template <typename Set>
auto nano::receivable_iterator<Set>::operator++ () -> receivable_iterator<Set> &
{
	item = set->receivable_lower_bound (*transaction, item.value ().first.account, item.value ().first.hash.number () + 1);
	if (item && item.value ().first.account != account)
	{
		item = std::nullopt;
	}
	return *this;
}

template <typename Set>
std::pair<nano::pending_key, nano::pending_info> const & nano::receivable_iterator<Set>::operator* () const
{
	return item.value ();
}

template <typename Set>
std::pair<nano::pending_key, nano::pending_info> const * nano::receivable_iterator<Set>::operator->() const
{
	return &item.value ();
}
