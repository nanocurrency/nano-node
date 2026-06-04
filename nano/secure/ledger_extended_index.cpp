#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/extended/account_delegator_by_weight.hpp>
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
			store.version.put_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled, false);
			store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, false);
			store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, false);
		}
		flags.account_delegator_by_weight_index = false;
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
	if (!flags.any_extended_ledger_index_disabled ())
	{
		return;
	}

	logger.info (nano::log::type::ledger_upgrade, "Populating extended ledger indices...");

	if (!flags.account_delegator_by_weight_index)
	{
		populate_account_delegator_by_weight_index ();
	}
	if (!flags.account_receivable_by_amount_index)
	{
		populate_account_receivable_by_amount_index ();
	}
	if (!flags.receive_block_by_send_block_index)
	{
		populate_receive_block_by_send_block_index ();
	}

	logger.info (nano::log::type::ledger_upgrade, "Extended ledger indices populated");
}

void nano::ledger::drop_extended_ledger_indices ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be dropped while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Dropping extended ledger indices...");

	{
		auto txn = store.tx_begin_write ();
		store.version.put_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled, false);
		store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, false);
		store.version.put_flag (txn, nano::store::meta_key::receive_block_by_send_block_index_enabled, false);
	}

	logger.info (nano::log::type::ledger_upgrade, "Dropping delegator weight index...");
	store.account_delegator_by_weight.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Delegator weight index dropped");

	logger.info (nano::log::type::ledger_upgrade, "Dropping receivable amount index...");
	store.account_receivable_by_amount.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Receivable amount index dropped");

	logger.info (nano::log::type::ledger_upgrade, "Dropping receive block lookup index...");
	store.receive_block_by_send_block.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Receive block lookup index dropped");

	flags.account_delegator_by_weight_index = false;
	flags.account_receivable_by_amount_index = false;
	flags.receive_block_by_send_block_index = false;

	logger.info (nano::log::type::ledger_upgrade, "Extended ledger indices dropped");
}

void nano::ledger::populate_receive_block_by_send_block_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating receive block lookup index...");

	uint64_t total_blocks = 0;
	{
		auto txn = store.tx_begin_read ();
		total_blocks = store.block.count (txn);
	}

	logger.info (nano::log::type::ledger_upgrade, "Clearing receive block lookup index...");
	store.receive_block_by_send_block.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Receive block lookup index cleared");

	size_t const batch_size_compute = nano::is_dev_run () ? 2 : 1024 * 1024;

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		size_t processed = 0;
		for (auto i = store.block.begin (txn), n = store.block.end (txn); i != n; ++i, ++processed)
		{
			if (processed % batch_size_compute == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Receive block lookup index progress: {} / {} blocks ({:.1f}%)", processed, total_blocks, nano::log::percentage (processed, total_blocks));
			}

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
	logger.info (nano::log::type::ledger_upgrade, "Receive block lookup index populated with {} entries", indexed);
}

void nano::ledger::populate_account_delegator_by_weight_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating delegator weight index...");

	size_t total_accounts = 0;
	{
		auto txn = store.tx_begin_read ();
		total_accounts = store.account.count (txn);
	}

	logger.info (nano::log::type::ledger_upgrade, "Clearing delegator weight index...");
	store.account_delegator_by_weight.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Delegator weight index cleared");

	size_t const batch_size_compute = nano::is_dev_run () ? 2 : 1024 * 1024;

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		size_t processed = 0;
		for (auto i = store.account.begin (txn), n = store.account.end (txn); i != n; ++i, ++processed)
		{
			if (processed % batch_size_compute == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Delegator weight index progress: {} / {} accounts ({:.1f}%)", processed, total_accounts, nano::log::percentage (processed, total_accounts));
			}

			store.account_delegator_by_weight.put (txn, { i->second.representative, i->second.balance, i->first });
			++indexed;
		}
		store.version.put_flag (txn, nano::store::meta_key::account_delegator_by_weight_index_enabled, true);
	}

	flags.account_delegator_by_weight_index = true;
	logger.info (nano::log::type::ledger_upgrade, "Delegator weight index populated with {} entries", indexed);
}

void nano::ledger::populate_account_receivable_by_amount_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating receivable amount index...");

	uint64_t total_receivables = 0;
	{
		auto txn = store.tx_begin_read ();
		total_receivables = store.pending.count (txn);
	}

	logger.info (nano::log::type::ledger_upgrade, "Clearing receivable amount index...");
	store.account_receivable_by_amount.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Receivable amount index cleared");

	size_t const batch_size_compute = nano::is_dev_run () ? 2 : 1024 * 1024;

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		size_t processed = 0;
		for (auto i = store.pending.begin (txn), n = store.pending.end (txn); i != n; ++i, ++processed)
		{
			if (processed % batch_size_compute == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Receivable amount index progress: {} / {} receivables ({:.1f}%)", processed, total_receivables, nano::log::percentage (processed, total_receivables));
			}

			store.account_receivable_by_amount.put (txn, { i->first.account, i->second.amount, i->first.hash }, { i->second.source, i->second.epoch });
			++indexed;
		}
		store.version.put_flag (txn, nano::store::meta_key::account_receivable_by_amount_index_enabled, true);
	}

	flags.account_receivable_by_amount_index = true;
	logger.info (nano::log::type::ledger_upgrade, "Receivable amount index populated with {} entries", indexed);
}
