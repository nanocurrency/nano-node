#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/extended/account_block_by_height.hpp>
#include <nano/store/ledger/extended/account_delegator_by_weight.hpp>
#include <nano/store/ledger/extended/account_receivable_by_amount.hpp>
#include <nano/store/ledger/extended/receive_block_by_send_block.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/successor.hpp>

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
			store.version.put_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled, false);
		}
		flags.account_delegator_by_weight_index = false;
		flags.account_receivable_by_amount_index = false;
		flags.receive_block_by_send_block_index = false;
		flags.account_block_by_height_index = false;
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
	if (!flags.account_block_by_height_index)
	{
		populate_account_block_by_height_index ();
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
		store.version.put_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled, false);
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

	logger.info (nano::log::type::ledger_upgrade, "Dropping account block height index...");
	store.account_block_by_height.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Account block height index dropped");

	flags.account_delegator_by_weight_index = false;
	flags.account_receivable_by_amount_index = false;
	flags.receive_block_by_send_block_index = false;
	flags.account_block_by_height_index = false;

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

void nano::ledger::populate_account_block_by_height_index ()
{
	release_assert (store.get_mode () != nano::store::open_mode::read_only, "extended ledger indices cannot be populated while the backend is opened in read-only mode");

	logger.info (nano::log::type::ledger_upgrade, "Populating account block height index...");

	uint64_t total_blocks = 0;
	{
		auto txn = store.tx_begin_read ();
		total_blocks = store.block.count (txn);
	}

	logger.info (nano::log::type::ledger_upgrade, "Clearing account block height index...");
	store.account_block_by_height.clear ();
	logger.info (nano::log::type::ledger_upgrade, "Account block height index cleared");

	size_t const batch_size_compute = nano::is_dev_run () ? 2 : 1024 * 1024;

	uint64_t indexed{ 0 };
	{
		auto txn = store.tx_begin_write ();
		for (auto i = store.account.begin (txn), n = store.account.end (txn); i != n; ++i)
		{
			auto const & account = i->first;
			auto const & info = i->second;
			release_assert (!info.open_block.is_zero (), "Account open block must be non-zero for account block height indexing", account.to_string ());
			release_assert (!info.head.is_zero (), "Account head block must be non-zero for account block height indexing", account.to_string ());
			release_assert (info.block_count != 0, "Account block count must be non-zero for account block height indexing", account.to_string ());

			auto hash = info.open_block;
			for (uint64_t height = 1; height <= info.block_count; ++height)
			{
				if (indexed % batch_size_compute == 0)
				{
					logger.info (nano::log::type::ledger_upgrade, "Account block height index progress: {} / {} blocks ({:.1f}%)", indexed, total_blocks, nano::log::percentage (indexed, total_blocks));
				}

				release_assert (!hash.is_zero (), "Account chain ended before account block count during account block height indexing", account.to_string ());
				auto const block = store.block.get (txn, hash);
				release_assert (block, "Missing account-chain block during account block height indexing", hash.to_string ());
				release_assert (block->account () == account, "Account-chain block account mismatch during account block height indexing", hash.to_string ());
				release_assert (block->sideband ().height == height, "Account-chain block height mismatch during account block height indexing", hash.to_string ());

				store.account_block_by_height.put (txn, { account, height }, hash);
				++indexed;

				if (height == info.block_count)
				{
					release_assert (hash == info.head, "Account chain block count ended before account head during account block height indexing", account.to_string ());
				}
				else
				{
					release_assert (hash != info.head, "Account head reached before account block count during account block height indexing", account.to_string ());
					auto successor = store.successor.get (txn, hash);
					release_assert (successor.has_value (), "Missing account-chain successor during account block height indexing", hash.to_string ());
					hash = successor.value ();
				}
			}
		}
		release_assert (indexed == total_blocks, "account block height index entry count mismatch");
		store.version.put_flag (txn, nano::store::meta_key::account_block_by_height_index_enabled, true);
	}

	flags.account_block_by_height_index = true;
	logger.info (nano::log::type::ledger_upgrade, "Account block height index populated with {} entries", indexed);
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
