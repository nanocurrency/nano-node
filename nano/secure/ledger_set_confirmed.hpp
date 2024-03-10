#pragma once

#include <optional>

namespace nano
{
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
	ledger_set_confirmed (nano::ledger const & ledger);

public: // Operations on accounts
	std::optional<nano::amount> account_balance (store::transaction const & transaction, nano::account const & account) const;
	nano::block_hash account_head (store::transaction const & transaction, nano::account const & account) const;
	uint64_t account_height (store::transaction const & transaction, nano::account const & account) const;

public: // Operations on blocks
	std::optional<nano::amount> block_balance (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists (store::transaction const & transaction, nano::block_hash const & hash) const;
	bool block_exists_or_pruned (store::transaction const & transaction, nano::block_hash const & hash) const;
	std::shared_ptr<nano::block> block_get (store::transaction const & transaction, nano::block_hash const & hash) const;

private:
	nano::ledger const & ledger;
}; // class ledger_set_confirmed
} // namespace nano
