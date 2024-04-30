#pragma once

#include <nano/secure/receivable_iterator.hpp>

#include <optional>

namespace nano
{
class account_info;
class block;
class block_hash;
class ledger;
class qualified_root;
}
namespace nano::store
{
class transaction;
}

namespace nano
{
class ledger_set_confirmed
{
public:
	using receivable_iterator = nano::receivable_iterator<ledger_set_confirmed>;

	ledger_set_confirmed (nano::ledger const & ledger);

public: // Operations on accounts
	std::optional<nano::amount> account_balance (secure::transaction const & transaction, nano::account const & account) const;
	nano::block_hash account_head (secure::transaction const & transaction, nano::account const & account) const;
	uint64_t account_height (secure::transaction const & transaction, nano::account const & account) const;

public: // Operations on blocks
	std::optional<nano::amount> block_balance (secure::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists (secure::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists_or_pruned (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::shared_ptr<nano::block> block_get (secure::transaction const & transaction, nano::block_hash const & hash) const;

public: // Operations on pending entries
	receivable_iterator receivable_end () const;
	// Returns the next receivable entry equal or greater than 'key'
	// Mirrors std::map::lower_bound
	std::optional<std::pair<nano::pending_key, nano::pending_info>> receivable_lower_bound (secure::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const;
	// Returns the next receivable entry for an account greater than 'account'
	// Returns receivable_lower_bound (transaction, account + 1, 0)
	// Mirrors std::map::upper_bound
	receivable_iterator receivable_upper_bound (secure::transaction const & transaction, nano::account const & account) const;
	// Returns the next receivable entry for the account 'account' with hash greater than 'hash'
	// Returns receivable_lower_bound (transaction, account + 1, hash)
	// Mirrors std::map::upper_bound
	receivable_iterator receivable_upper_bound (secure::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const;

private:
	nano::ledger const & ledger;
}; // class ledger_set_confirmed
} // namespace nano
