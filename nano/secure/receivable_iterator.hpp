#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/secure/pending_info.hpp>

#include <optional>
#include <utility>

namespace nano
{
// This class iterates receivable enttries for an account
template <typename Set>
class receivable_iterator
{
public:
	// Creates an end () iterator
	// 'transaction' and 'set' are nullptr so all end () iterators compare equal
	// 'account' is set to 0 so all end () iterators compare equal.
	receivable_iterator ();
	// Constructs an iterator to the first equal to pending_key.account and equal or greater than pending_key.hash
	receivable_iterator (secure::transaction const & transaction, Set const & set, std::optional<std::pair<nano::pending_key, nano::pending_info>> const & item);
	bool operator== (receivable_iterator const & other) const;

public: // Dereferencing, undefined behavior when called on an end () iterator
	// Advances to the next receivable entry
	// This will not advance past the account in the pending_key it was constructed with
	// Not advancing past an account is intentional to prevent
	// If there are no more pending entries, convert this to an end () iterator.
	receivable_iterator & operator++ ();
	std::pair<nano::pending_key, nano::pending_info> const & operator* () const;
	std::pair<nano::pending_key, nano::pending_info> const * operator->() const;

private:
	secure::transaction const * transaction;
	Set const * set{ nullptr };
	nano::account account{ 0 };
	std::optional<std::pair<nano::pending_key, nano::pending_info>> item;
};
}
