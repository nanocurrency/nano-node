#pragma once

#include <nano/secure/account_iterator.hpp>
#include <nano/secure/receivable_iterator.hpp>

#include <optional>

namespace nano
{
class account_info;
class block;
class block_hash;
class ledger;
class pending_info;
class pending_key;
class qualified_root;
}
namespace nano::store
{
class transaction;
}

namespace nano
{
class ledger_set_any
{
public:
	using account_iterator = nano::account_iterator<ledger_set_any>;
	using receivable_iterator = nano::receivable_iterator<ledger_set_any>;

	ledger_set_any (nano::ledger const & ledger);

public: // Operations on accounts
	std::optional<nano::amount> account_balance (secure::transaction const & transaction, nano::account const & account) const;
	account_iterator account_begin (secure::transaction const & transaction) const;
	account_iterator account_end () const;
	std::optional<nano::account_info> account_get (secure::transaction const & transaction, nano::account const & account) const;
	nano::block_hash account_head (secure::transaction const & transaction, nano::account const & account) const;
	uint64_t account_height (secure::transaction const & transaction, nano::account const & account) const;
	// Returns the next account entry equal or greater than 'account'
	// Mirrors std::map::lower_bound
	account_iterator account_lower_bound (secure::transaction const & transaction, nano::account const & account) const;
	// Returns the next account entry greater than 'account'
	// Returns account_lower_bound (transaction, account + 1)
	// Mirrors std::map::upper_bound
	account_iterator account_upper_bound (secure::transaction const & transaction, nano::account const & account) const;

public: // Operations on blocks
	std::optional<nano::account> block_account (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::amount> block_amount (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::amount> block_balance (secure::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists (secure::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists_or_pruned (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::shared_ptr<nano::block> block_get (secure::transaction const & transaction, nano::block_hash const & hash) const;
	uint64_t block_height (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::block_hash> block_successor (secure::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::block_hash> block_successor (secure::transaction const & transaction, nano::qualified_root const & root) const;

public: // Operations on pending entries
	std::optional<nano::pending_info> pending_get (secure::transaction const & transaction, nano::pending_key const & key) const;
	receivable_iterator receivable_end () const;
	bool receivable_exists (secure::transaction const & transaction, nano::account const & account) const;
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
}; // class ledger_set_any
} // namespace nano
