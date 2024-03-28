#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_view_confirmed.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>

nano::ledger_view_confirmed::ledger_view_confirmed (nano::ledger const & ledger) :
	ledger{ ledger }
{
}

std::optional<nano::uint128_t> nano::ledger_view_confirmed::balance (store::transaction const & transaction, nano::account const & account_a) const
{
	auto block = get (transaction, head (transaction, account_a));
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ().number ();
}

// Balance for account containing hash
std::optional<nano::uint128_t> nano::ledger_view_confirmed::balance (store::transaction const & transaction, nano::block_hash const & hash) const
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

bool nano::ledger_view_confirmed::exists (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return ledger.store.block.exists (transaction, hash);
}

bool nano::ledger_view_confirmed::exists_or_pruned (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return ledger.store.pruned.exists (transaction, hash) || exists (transaction, hash);
}

std::optional<nano::account_info> nano::ledger_view_confirmed::get (store::transaction const & transaction, nano::account const & account) const
{
	return ledger.store.account.get (transaction, account);
}

std::shared_ptr<nano::block> nano::ledger_view_confirmed::get (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return ledger.store.block.get (transaction, hash);
}

std::optional<nano::pending_info> nano::ledger_view_confirmed::get (store::transaction const & transaction, nano::pending_key const & key) const
{
	return ledger.store.pending.get (transaction, key);
}

nano::block_hash nano::ledger_view_confirmed::head (store::transaction const & transaction, nano::account const & account) const
{
	auto info = get (transaction, account);
	if (!info)
	{
		return 0;
	}
	return info.value ().head;
}

uint64_t nano::ledger_view_confirmed::height (store::transaction const & transaction, nano::account const & account) const
{
	auto head_l = head (transaction, account);
	if (head_l.is_zero ())
	{
		return 0;
	}
	return get (transaction, head_l)->sideband ().height;
}

auto nano::ledger_view_confirmed::receivable_end () const -> receivable_iterator
{
	return receivable_iterator{};
}

auto nano::ledger_view_confirmed::receivable_upper_bound (store::transaction const & transaction, nano::account const & account) const -> receivable_iterator
{
	return receivable_iterator{ transaction, *this, receivable_lower_bound (transaction, account.number () + 1, 0) };
}

auto nano::ledger_view_confirmed::receivable_upper_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const -> receivable_iterator
{
	auto result = receivable_lower_bound (transaction, account, hash.number () + 1);
	if (!result || result.value ().first.account != account)
	{
		return receivable_iterator{ transaction, *this, std::nullopt };
	}
	return receivable_iterator{ transaction, *this, result };
}

std::optional<std::pair<nano::pending_key, nano::pending_info>> nano::ledger_view_confirmed::receivable_lower_bound (store::transaction const & transaction, nano::account const & account, nano::block_hash const & hash) const
{
	auto result = ledger.store.pending.begin (transaction, { account, hash });
	while (result != ledger.store.pending.end () && !exists (transaction, result->first.hash))
	{
		++result;
	}
	if (result == ledger.store.pending.end ())
	{
		return std::nullopt;
	}
	return *result;
}

std::optional<nano::block_hash> nano::ledger_view_confirmed::successor (store::transaction const & transaction, nano::qualified_root const & root) const
{
	if (root.previous ().is_zero ())
	{
		return ledger.store.block.successor (transaction, root.previous ());
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
