#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>

#include <optional>
#include <utility>

namespace nano::secure
{
class transaction;
}

namespace nano
{
// This class iterates account entries
template <typename Set>
class account_iterator
{
public:
	// Creates an end () iterator
	// 'transaction' and 'set' are nullptr so all end () iterators compare equal
	account_iterator ();
	account_iterator (secure::transaction const & transaction, Set const & set, std::optional<std::pair<nano::account, nano::account_info>> const & item);

	// Compares if these iterators hold the same 'item'.
	// Undefined behavior if this and other don't hold the same 'set' and 'transaction'
	bool operator== (account_iterator const & other) const;

public: // Dereferencing, undefined behavior when called on an end () iterator
	// Advances the iterator to the next greater nano::account
	// If there are no more accounts, convert this to an end () iterator
	account_iterator & operator++ ();
	std::pair<nano::account, nano::account_info> const & operator* () const;
	std::pair<nano::account, nano::account_info> const * operator->() const;

private:
	secure::transaction const * transaction;
	Set const * set{ nullptr };
	// Current item at the position of the iterator
	// std::nullopt if an end () iterator
	std::optional<std::pair<nano::account, nano::account_info>> item;
};
}
