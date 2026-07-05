#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/extended/account_receivable_by_amount.hpp>
#include <nano/store/ledger/extended/receive_block_by_send_block.hpp>
#include <nano/store/ledger/pending.hpp>

void nano::ledger::initialize_extended_ledger_indices ()
{
	if (!options.enable_extended_ledger_index)
	{
		if (flags.any_extended_ledger_index_enabled () && store.get_mode () != nano::store::open_mode::read_only)
		{
			logger.warn (nano::log::type::ledger, "Extended ledger index is disabled; existing extended ledger indices will be marked disabled and ignored");
			auto txn = store.tx_begin_write ();
			store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, false);
			store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, false);
		}
		flags.account_receivable_by_amount_index = false;
		flags.receive_block_by_send_block_index = false;
		return;
	}

	if (store.get_mode () == nano::store::open_mode::read_only)
	{
		return;
	}

	populate_extended_ledger_indices ();
}

void nano::ledger::populate_extended_ledger_indices ()
{
	if (!flags.account_receivable_by_amount_index)
	{
		populate_account_receivable_by_amount_index ();
	}
	if (!flags.receive_block_by_send_block_index)
	{
		populate_receive_block_by_send_block_index ();
	}
}

void nano::ledger::drop_extended_ledger_indices ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be dropped while the backend is opened in read-only mode");

	{
		auto txn = store.tx_begin_write ();
		store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, false);
		store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, false);
	}

	store.account_receivable_by_amount.clear ();
	store.receive_block_by_send_block.clear ();

	flags.account_receivable_by_amount_index = false;
	flags.receive_block_by_send_block_index = false;
}

void nano::ledger::populate_receive_block_by_send_block_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating receive block by send block index...");
	store.receive_block_by_send_block.clear ();

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		for (auto i = store.block.begin (txn), n = store.block.end (txn); i != n; ++i)
		{
			auto const & block = i->second.block;
			if (block->is_receive ())
			{
				store.receive_block_by_send_block.put (txn, block->source (), i->first);
				++indexed;
			}
		}
		store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, true);
	}

	flags.receive_block_by_send_block_index = true;
	logger.info (nano::log::type::ledger_upgrade, "Done populating receive block by send block index with {} entries", indexed);
}

void nano::ledger::populate_account_receivable_by_amount_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating account receivables by amount index...");
	store.account_receivable_by_amount.clear ();

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		for (auto i = store.pending.begin (txn), n = store.pending.end (txn); i != n; ++i)
		{
			store.account_receivable_by_amount.put (txn, { i->first.account, i->second.amount, i->first.hash }, { i->second.source, i->second.epoch });
			++indexed;
		}
		store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, true);
	}

	flags.account_receivable_by_amount_index = true;
	logger.info (nano::log::type::ledger_upgrade, "Done populating account receivables by amount index with {} entries", indexed);
}
