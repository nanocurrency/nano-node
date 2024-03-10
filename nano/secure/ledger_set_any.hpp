#pragma once

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
	ledger_set_any (nano::ledger const & ledger);

public: // Operations on accounts
	std::optional<nano::amount> account_balance (store::transaction const & transaction, nano::account const & account) const;
	std::optional<nano::account_info> account_get (store::transaction const & transaction, nano::account const & account) const;
	nano::block_hash account_head (store::transaction const & transaction, nano::account const & account) const;
	uint64_t account_height (store::transaction const & transaction, nano::account const & account) const;

public: // Operations on blocks
	std::optional<nano::account> block_account (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::amount> block_amount (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::amount> block_balance (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists_or_pruned (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::shared_ptr<nano::block> block_get (store::transaction const & transaction, nano::block_hash const & hash) const;
	uint64_t block_height (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::block_hash> block_successor (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::optional<nano::block_hash> block_successor (store::transaction const & transaction, nano::qualified_root const & root) const;

public: // Operations on pending entries
	std::optional<nano::pending_info> pending_get (store::transaction const & transaction, nano::pending_key const & key) const;

private:
	nano::ledger const & ledger;
}; // class ledger_set_any
} // namespace nano
