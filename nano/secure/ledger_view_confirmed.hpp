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
class ledger_view_confirmed
{
public:
	using receivable_iterator = nano::receivable_iterator<ledger_view_confirmed>;

	ledger_view_confirmed (nano::ledger const & ledger);

	std::optional<nano::uint128_t> balance (store::transaction const & transaction, nano::account const & account) const;
	std::optional<nano::uint128_t> balance (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool exists (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool exists_or_pruned (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::account_info> get (store::transaction const & transaction, nano::account const & account) const;
	std::shared_ptr<nano::block> get (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::pending_info> get (store::transaction const & transaction, nano::pending_key const & key) const;
	nano::block_hash head (store::transaction const & transaction, nano::account const & account) const;
	uint64_t height (store::transaction const & transaction, nano::account const & account) const;
	receivable_iterator receivable_end () const;
	// Returns the next receivable entry equal or greater than 'key'
	std::optional<std::pair<nano::pending_key, nano::pending_info>> receivable_lower_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const;
	// Returns the next receivable entry for an account greater than 'account'
	receivable_iterator receivable_upper_bound (store::transaction const & transaction, nano::account const & account) const;
	// Returns the next receivable entry for the account 'account' with hash greater than 'hash'
	receivable_iterator receivable_upper_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const;
	std::optional<nano::block_hash> successor (store::transaction const & transaction, nano::qualified_root const & root) const;

private:
	nano::ledger const & ledger;
}; // class ledger_view_confirmed
} // namespace nano
