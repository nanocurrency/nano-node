#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/pending.hpp>
#include <celerix/store/pruned.hpp>

celerix::ledger_set_any::ledger_set_any (celerix::ledger const & ledger) :
	ledger{ ledger }
{
}

std::optional<celerix::amount> celerix::ledger_set_any::account_balance (secure::transaction const & transaction, celerix::account const & account_a) const
{
	auto block = block_get (transaction, account_head (transaction, account_a));
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

auto celerix::ledger_set_any::account_begin (secure::transaction const & transaction) const -> account_iterator
{
	return account_lower_bound (transaction, 0);
}

auto celerix::ledger_set_any::account_end () const -> account_iterator
{
	return account_iterator{};
}

std::optional<celerix::account_info> celerix::ledger_set_any::account_get (secure::transaction const & transaction, celerix::account const & account) const
{
	return ledger.store.account.get (transaction, account);
}

celerix::block_hash celerix::ledger_set_any::account_head (secure::transaction const & transaction, celerix::account const & account) const
{
	auto info = account_get (transaction, account);
	if (!info)
	{
		return 0;
	}
	return info.value ().head;
}

uint64_t celerix::ledger_set_any::account_height (secure::transaction const & transaction, celerix::account const & account) const
{
	auto head_l = account_head (transaction, account);
	if (head_l.is_zero ())
	{
		return 0;
	}
	auto block = block_get (transaction, head_l);
	release_assert (block); // Head block must be in ledger
	return block->sideband ().height;
}

auto celerix::ledger_set_any::account_lower_bound (secure::transaction const & transaction, celerix::account const & account) const -> account_iterator
{
	auto disk = ledger.store.account.begin (transaction, account);
	if (disk == ledger.store.account.end (transaction))
	{
		return account_iterator{};
	}
	return account_iterator{ transaction, *this, *disk };
}

auto celerix::ledger_set_any::account_upper_bound (secure::transaction const & transaction, celerix::account const & account) const -> account_iterator
{
	return account_lower_bound (transaction, account.number () + 1);
}

std::optional<celerix::account> celerix::ledger_set_any::block_account (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	auto block_l = block_get (transaction, hash);
	if (!block_l)
	{
		return std::nullopt;
	}
	return block_l->account ();
}

std::optional<celerix::amount> celerix::ledger_set_any::block_amount (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	auto block_l = block_get (transaction, hash);
	if (!block_l)
	{
		return std::nullopt;
	}
	return block_amount (transaction, block_l);
}

std::optional<celerix::amount> celerix::ledger_set_any::block_amount (secure::transaction const & transaction, std::shared_ptr<celerix::block> const & block) const
{
	auto block_balance = block->balance ();
	if (block->previous ().is_zero ())
	{
		return block_balance.number ();
	}
	auto previous_balance = this->block_balance (transaction, block->previous ());
	if (!previous_balance)
	{
		return std::nullopt;
	}
	return block_balance > previous_balance.value () ? block_balance.number () - previous_balance.value ().number () : previous_balance.value ().number () - block_balance.number ();
}

std::optional<celerix::amount> celerix::ledger_set_any::block_balance (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return std::nullopt;
	}
	auto block = block_get (transaction, hash);
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

bool celerix::ledger_set_any::block_exists (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return false;
	}
	return ledger.store.block.exists (transaction, hash);
}

bool celerix::ledger_set_any::block_exists_or_pruned (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return false;
	}
	if (ledger.store.pruned.exists (transaction, hash))
	{
		return true;
	}
	return ledger.store.block.exists (transaction, hash);
}

std::shared_ptr<celerix::block> celerix::ledger_set_any::block_get (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return nullptr;
	}
	return ledger.store.block.get (transaction, hash);
}

uint64_t celerix::ledger_set_any::block_height (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	auto block = block_get (transaction, hash);
	if (!block)
	{
		return 0;
	}
	return block->sideband ().height;
}

std::optional<std::pair<celerix::pending_key, celerix::pending_info>> celerix::ledger_set_any::receivable_lower_bound (secure::transaction const & transaction, celerix::account const & account, celerix::block_hash const & hash) const
{
	auto result = ledger.store.pending.begin (transaction, { account, hash });
	if (result == ledger.store.pending.end (transaction))
	{
		return std::nullopt;
	}
	return *result;
}

auto celerix::ledger_set_any::receivable_end () const -> receivable_iterator
{
	return receivable_iterator{};
}

bool celerix::ledger_set_any::receivable_exists (secure::transaction const & transaction, celerix::account const & account) const
{
	auto next = receivable_upper_bound (transaction, account, 0);
	return next != receivable_end ();
}

auto celerix::ledger_set_any::receivable_upper_bound (secure::transaction const & transaction, celerix::account const & account) const -> receivable_iterator
{
	return receivable_iterator{ transaction, *this, receivable_lower_bound (transaction, account.number () + 1, 0) };
}

auto celerix::ledger_set_any::receivable_upper_bound (secure::transaction const & transaction, celerix::account const & account, celerix::block_hash const & hash) const -> receivable_iterator
{
	auto result = receivable_lower_bound (transaction, account, hash.number () + 1);
	if (!result || result.value ().first.account != account)
	{
		return receivable_iterator{ transaction, *this, std::nullopt };
	}
	return receivable_iterator{ transaction, *this, result };
}

std::optional<celerix::block_hash> celerix::ledger_set_any::block_successor (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	return block_successor (transaction, { hash, hash });
}

std::optional<celerix::block_hash> celerix::ledger_set_any::block_successor (secure::transaction const & transaction, celerix::qualified_root const & root) const
{
	if (!root.previous ().is_zero ())
	{
		return ledger.store.block.successor (transaction, root.previous ());
	}
	else
	{
		auto info = account_get (transaction, root.root ().as_account ());
		if (info)
		{
			return info->open_block;
		}
		else
		{
			return std::nullopt;
		}
	}
}

std::optional<celerix::pending_info> celerix::ledger_set_any::pending_get (secure::transaction const & transaction, celerix::pending_key const & key) const
{
	return ledger.store.pending.get (transaction, key);
}
