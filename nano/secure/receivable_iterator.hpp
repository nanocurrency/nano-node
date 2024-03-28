#pragma once
#include <nano/lib/numbers.hpp>
#include <nano/secure/pending_info.hpp>

#include <optional>
#include <utility>

namespace nano
{
// This class iterates receivable enttries for an account
template <typename T>
class receivable_iterator
{
public:
	receivable_iterator ();
	receivable_iterator (store::transaction const & transaction, T const & view, std::optional<std::pair<nano::pending_key, nano::pending_info>> const & item);
	bool operator== (receivable_iterator const & other) const;
	bool operator!= (receivable_iterator const & other) const;
	// Advances to the next receivable entry for the same account
	receivable_iterator & operator++ ();
	std::pair<nano::pending_key, nano::pending_info> const & operator* () const;
	std::pair<nano::pending_key, nano::pending_info> const * operator->() const;

private:
	store::transaction const * transaction;
	T const * view{ nullptr };
	nano::account account{ 0 };
	std::optional<std::pair<nano::pending_key, nano::pending_info>> item;
};
}
