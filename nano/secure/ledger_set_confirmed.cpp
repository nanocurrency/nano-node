#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/confirmation_height.hpp>
#include <celerix/store/pending.hpp>
#include <celerix/store/pruned.hpp>

celerix::ledger_set_confirmed::ledger_set_confirmed (celerix::ledger const & ledger) :
	ledger{ ledger }
{
}

std::optional<celerix::amount> celerix::ledger_set_confirmed::account_balance (secure::transaction const & transaction, celerix::account const & account_a) const
{
	auto block = block_get (transaction, account_head (transaction, account_a));
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

celerix::block_hash celerix::ledger_set_confirmed::account_head (secure::transaction const & transaction, celerix::account const & account) const
{
	auto info = ledger.store.confirmation_height.get (transaction, account);
	if (!info)
	{
		return 0;
	}
	return info.value ().frontier;
}

uint64_t celerix::ledger_set_confirmed::account_height (secure::transaction const & transaction, celerix::account const & account) const
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

std::optional<celerix::amount> celerix::ledger_set_confirmed::block_balance (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	auto block = block_get (transaction, hash);
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ();
}

bool celerix::ledger_set_confirmed::block_exists (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	return block_get (transaction, hash) != nullptr;
}

bool celerix::ledger_set_confirmed::block_exists_or_pruned (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return false;
	}
	if (ledger.store.pruned.exists (transaction, hash))
	{
		return true;
	}
	return block_exists (transaction, hash);
}

std::shared_ptr<celerix::block> celerix::ledger_set_confirmed::block_get (secure::transaction const & transaction, celerix::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return nullptr;
	}
	auto block = ledger.store.block.get (transaction, hash);
	if (!block)
	{
		return nullptr;
	}
	auto info = ledger.store.confirmation_height.get (transaction, block->account ());
	if (!info)
	{
		return nullptr;
	}
	return block->sideband ().height <= info.value ().height ? block : nullptr;
}
auto celerix::ledger_set_confirmed::receivable_end () const -> receivable_iterator
{
	return receivable_iterator{};
}

auto celerix::ledger_set_confirmed::receivable_upper_bound (secure::transaction const & transaction, celerix::account const & account) const -> receivable_iterator
{
	return receivable_iterator{ transaction, *this, receivable_lower_bound (transaction, account.number () + 1, 0) };
}

auto celerix::ledger_set_confirmed::receivable_upper_bound (secure::transaction const & transaction, celerix::account const & account, celerix::block_hash const & hash) const -> receivable_iterator
{
	auto result = receivable_lower_bound (transaction, account, hash.number () + 1);
	if (!result || result.value ().first.account != account)
	{
		return receivable_iterator{ transaction, *this, std::nullopt };
	}
	return receivable_iterator{ transaction, *this, result };
}

std::optional<std::pair<celerix::pending_key, celerix::pending_info>> celerix::ledger_set_confirmed::receivable_lower_bound (secure::transaction const & transaction, celerix::account const & account, celerix::block_hash const & hash) const
{
	auto result = ledger.store.pending.begin (transaction, { account, hash });
	while (result != ledger.store.pending.end (transaction) && !block_exists (transaction, result->first.hash))
	{
		++result;
	}
	if (result == ledger.store.pending.end (transaction))
	{
		return std::nullopt;
	}
	return *result;
}
