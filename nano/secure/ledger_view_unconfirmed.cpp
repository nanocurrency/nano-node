#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_view_unconfirmed.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>

nano::ledger_view_unconfirmed::ledger_view_unconfirmed (nano::ledger const & ledger) :
	ledger{ ledger }
{
}

std::optional<nano::account> nano::ledger_view_unconfirmed::account (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = get (transaction, hash);
	if (!block_l)
	{
		return std::nullopt;
	}
	return block_l->account ();
}

std::optional<nano::uint128_t> nano::ledger_view_unconfirmed::amount (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = get (transaction, hash);
	if (!block_l)
	{
		return std::nullopt;
	}
	auto block_balance = block_l->balance ();
	if (block_l->previous ().is_zero ())
	{
		return block_balance.number ();
	}
	auto previous_balance = balance (transaction, block_l->previous ());
	if (!previous_balance)
	{
		return std::nullopt;
	}
	return block_balance > previous_balance.value () ? block_balance.number () - previous_balance.value () : previous_balance.value () - block_balance.number ();
}

std::optional<nano::uint128_t> nano::ledger_view_unconfirmed::balance (store::transaction const & transaction, nano::account const & account_a) const
{
	auto block = get (transaction, ledger->head (transaction, account_a));
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ().number ();
}

// Balance for account containing hash
std::optional<nano::uint128_t> nano::ledger_view_unconfirmed::balance (store::transaction const & transaction, nano::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return std::nullopt;
	}
	auto block = get (transaction, hash);
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ().number ();
}

bool nano::ledger_view_unconfirmed::exists (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return ledger.unconfirmed_set.block.count (hash) == 1 || ledger.store.block.exists (transaction, hash);
}

bool nano::ledger_view_unconfirmed::exists_or_pruned (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return exists (transaction, hash) || ledger.store.pruned.exists (transaction, hash);
}

std::optional<nano::account_info> nano::ledger_view_unconfirmed::get (store::transaction const & transaction, nano::account const & account) const
{
	return ledger.unconfirmed_set.account.count (account) == 1 ? ledger.unconfirmed_set.account.at (account) : ledger.store.account.get (transaction, account);
}

std::shared_ptr<nano::block> nano::ledger_view_unconfirmed::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return ledger.unconfirmed_set.block.count (hash) == 1 ? ledger.unconfirmed_set.block.at (hash).block : ledger.store.block.get (transaction, hash);
}

std::optional<nano::pending_info> nano::ledger_view_unconfirmed::get (store::transaction const & transaction, nano::pending_key const & key) const
{
	if (ledger.unconfirmed_set.received.count (key) != 0)
	{
		return std::nullopt;
	}
	if (ledger.unconfirmed_set.receivable.count (key) != 0)
	{
		return ledger.unconfirmed_set.receivable.at (key);
	}
	return ledger.store.pending.get (transaction, key);
}

nano::block_hash nano::ledger_view_unconfirmed::head (store::transaction const & transaction, nano::account const & account) const
{
	auto info = get (transaction, account);
	if (!info)
	{
		return 0;
	}
	return info.value ().head;
}

uint64_t nano::ledger_view_unconfirmed::height (store::transaction const & transaction, nano::account const & account) const
{
	auto head_l = head (transaction, account);
	if (head_l.is_zero ())
	{
		return 0;
	}
	return get (transaction, head_l)->sideband ().height;
}

uint64_t nano::ledger_view_unconfirmed::height (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block = get (transaction, hash);
	if (!block)
	{
		return 0;
	}
	return block->sideband ().height;
}

bool nano::ledger_view_unconfirmed::receivable_any (store::transaction const & transaction, nano::account const & account) const
{
	auto next = receivable_upper_bound (transaction, account, 0);
	return next != receivable_end ();
}

std::optional<std::pair<nano::pending_key, nano::pending_info>> nano::ledger_view_unconfirmed::receivable_lower_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const
{
	auto mem = ledger.unconfirmed_set.receivable.lower_bound ({ account, hash });
	while (mem != ledger.unconfirmed_set.receivable.end () && ledger.unconfirmed_set.received.count (mem->first) != 0)
	{
		++mem;
	}
	auto disk = ledger.store.pending.begin (transaction, { account, hash });
	while (disk != ledger.store.pending.end () && ledger.unconfirmed_set.received.count (disk->first) != 0)
	{
		++disk;
	}
	std::optional<std::pair<nano::pending_key, nano::pending_info>> mem_val;
	if (mem != ledger.unconfirmed_set.receivable.end ())
	{
		mem_val = *mem;
	}
	std::optional<std::pair<nano::pending_key, nano::pending_info>> disk_val;
	if (disk != ledger.store.pending.end ())
	{
		disk_val = *disk;
	}
	if (!mem_val)
	{
		return disk_val;
	}
	if (!disk_val)
	{
		return mem_val;
	}
	return mem_val.value ().first < disk_val.value ().first ? mem_val : disk_val;
}

auto nano::ledger_view_unconfirmed::receivable_end () const -> receivable_iterator
{
	return receivable_iterator{};
}

auto nano::ledger_view_unconfirmed::receivable_upper_bound (store::transaction const & transaction, nano::account const & account) const -> receivable_iterator
{
	return receivable_iterator{ transaction, *this, receivable_lower_bound (transaction, account.number () + 1, 0) };
}

auto nano::ledger_view_unconfirmed::receivable_upper_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const -> receivable_iterator
{
	auto result = receivable_lower_bound (transaction, account, hash.number () + 1);
	if (!result || result.value ().first.account != account)
	{
		return receivable_iterator{ transaction, *this, std::nullopt };
	}
	return receivable_iterator{ transaction, *this, result };
}

std::optional<nano::block_hash> nano::ledger_view_unconfirmed::successor (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return successor (transaction, { hash, hash });
}

std::optional<nano::block_hash> nano::ledger_view_unconfirmed::successor (store::transaction const & transaction, nano::qualified_root const & root) const
{
	if (!root.previous ().is_zero ())
	{
		return ledger.unconfirmed_set.successor.count (root.previous ()) == 1 ? ledger.unconfirmed_set.successor.at (root.previous ()) : ledger.store.block.successor (transaction, root.previous ());
	}
	else
	{
		auto info = get (transaction, root.root ().as_account ());
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
