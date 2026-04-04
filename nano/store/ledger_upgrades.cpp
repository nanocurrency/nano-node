#include <nano/lib/block_sideband.hpp>
#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stream.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/block_w_sideband.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/ledger/rep_weight.hpp>
#include <nano/store/ledger/version.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/store/typed_iterator_templ.hpp>

namespace nano::store
{
nano::store::column_schema const ledger_store::schema_v21{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::unchecked, "unchecked" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const ledger_store::schema_v22{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const ledger_store::schema_v23{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::rep_weights, "rep_weights" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const ledger_store::schema_v24{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::rep_weights, "rep_weights" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const ledger_store::schema_v25{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::rep_weights, "rep_weights" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::successor, "successor" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::meta, "meta" }
};

// Drop unchecked table
void ledger_store::upgrade_v21_to_v22 ()
{
	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v21 to v22...");

	backend.open (schema_v21, nano::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 21, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		bool dropped = backend.drop_table ("unchecked");
		release_assert (dropped, "failed to drop unchecked table during upgrade");

		auto transaction = backend.tx_begin_write ();
		backend.set_version (transaction, 22);
	}
	backend.close ();

	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v21 to v22 completed");
}

// Fill rep_weights table with all existing representatives and their vote weight
void ledger_store::upgrade_v22_to_v23 ()
{
	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v22 to v23...");

	// Open with schema_v23 so we can access rep_weights table
	// This allows us to clear it if a previous upgrade attempt failed halfway
	backend.open (schema_v23, nano::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 22, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		// Always clear rep_weights table entries to ensure it's empty before populating
		// This can happen if an upgrade was attempted but failed halfway through
		auto clear_status = backend.clear (nano::store::table::rep_weights);
		release_assert (backend.success (clear_status), "failed to clear rep_weights table during upgrade", backend.error_string (clear_status));

		auto transaction = backend.tx_begin_write ();

		release_assert (rep_weight.begin (backend.tx_begin_read ()) == rep_weight.end (transaction), "rep weights table must be empty before upgrading to v23");

		auto iterate_accounts = [this] (auto && func) {
			auto transaction = backend.tx_begin_read ();

			// Manually create v22 compatible iterator to read accounts
			auto it = nano::store::typed_iterator<nano::account, nano::account_info_v22>{ backend.begin (transaction, nano::store::table::accounts) };
			auto const end = nano::store::typed_iterator<nano::account, nano::account_info_v22>{ backend.end (transaction, nano::store::table::accounts) };

			for (; it != end; ++it)
			{
				auto const & account = it->first;
				auto const & account_info = it->second;

				func (account, account_info);
			}
		};

		// Smaller batch size for dev runs to potentially trigger edge cases
		const size_t batch_size = nano::is_dev_run () ? 2 : 250000;

		size_t processed = 0;
		iterate_accounts ([this, &transaction, &processed, batch_size] (nano::account const & account, nano::account_info_v22 const & account_info) {
			if (!account_info.balance.is_zero ())
			{
				nano::uint128_t total{ 0 };
				nano::store::db_val value;
				auto status = backend.get (transaction, nano::store::table::rep_weights, account_info.representative, value);
				if (backend.success (status))
				{
					total = nano::amount{ value }.number ();
				}
				total += account_info.balance.number ();
				status = backend.put (transaction, nano::store::table::rep_weights, account_info.representative, nano::amount{ total });
				backend.release_assert_success (status);
			}

			processed++;
			if (processed % batch_size == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Processed {} accounts", processed);
				transaction.refresh (); // Refresh to prevent excessive memory usage
			}
		});

		logger.info (nano::log::type::ledger_upgrade, "Done processing {} accounts", processed);
		version.put (transaction, 23);
	}
	backend.close ();

	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v22 to v23 completed");
}

// Drop frontiers table
void ledger_store::upgrade_v23_to_v24 ()
{
	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v23 to v24...");

	backend.open (schema_v23, nano::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 23, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		bool dropped = backend.drop_table ("frontiers");
		release_assert (dropped, "failed to drop frontiers table during upgrade");

		auto transaction = backend.tx_begin_write ();
		version.put (transaction, 24);
	}
	backend.close ();

	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v23 to v24 completed");
}

// Populate dedicated successor table from block sideband data
void ledger_store::upgrade_v24_to_v25 ()
{
	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v24 to v25...");

	// Open with schema_v25 so we have access to the successor table
	backend.open (schema_v25, nano::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 24, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		// Always clear successor table to ensure clean state before populating
		auto clear_result = backend.clear (nano::store::table::successor);
		release_assert (backend.success (clear_result), "failed to clear successor table during upgrade", backend.error_string (clear_result));

		auto transaction = backend.tx_begin_write ();

		// Smaller batch size for dev runs to potentially trigger edge cases
		const size_t batch_size = nano::is_dev_run () ? 2 : 250000;
		size_t processed = 0;
		auto const total_blocks = backend.count (backend.tx_begin_read (), nano::store::table::blocks);

		// Iterate all blocks using a separate read transaction
		// Use block_w_sideband_v25 to read old format with successor in sideband
		auto iterate_blocks = [this] (auto && func) {
			auto read_txn = backend.tx_begin_read ();
			auto it = nano::store::typed_iterator<nano::block_hash, nano::store::block_w_sideband_v25>{ backend.begin (read_txn, nano::store::table::blocks) };
			auto const end = nano::store::typed_iterator<nano::block_hash, nano::store::block_w_sideband_v25>{ backend.end (read_txn, nano::store::table::blocks) };
			for (; it != end; ++it)
			{
				auto const & [hash, block_w_sideband] = *it;
				func (hash, block_w_sideband);
			}
		};

		iterate_blocks ([this, &transaction, &processed, batch_size, total_blocks] (nano::block_hash const & hash, nano::store::block_w_sideband_v25 const & block_w_sideband) {
			// If successor is non-zero, write to successor table
			if (!block_w_sideband.sideband.successor.is_zero ())
			{
				auto status = backend.put (transaction, nano::store::table::successor, hash, block_w_sideband.sideband.successor);
				backend.release_assert_success (status);
			}

			processed++;
			if (processed % batch_size == 0)
			{
				double const percentage = total_blocks > 0 ? (static_cast<double> (processed) / total_blocks * 100.0) : 0.0;
				logger.info (nano::log::type::ledger_upgrade, "Processed {} blocks ({:.1f}%)", processed, percentage);
				transaction.refresh ();
			}
		});

		logger.info (nano::log::type::ledger_upgrade, "Done processing {} blocks", processed);
		version.put (transaction, 25);
	}
	backend.close ();

	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v24 to v25 completed");
}

// Remove successor from block sideband
void ledger_store::upgrade_v25_to_v26 ()
{
	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v25 to v26...");

	// Open with schema_v25
	backend.open (schema_v25, nano::store::open_mode::read_write);
	{
		release_assert (backend.get_version (backend.tx_begin_read ()) == 25, "unexpected version during upgrade", std::to_string (backend.get_version (backend.tx_begin_read ())));

		auto transaction = backend.tx_begin_write ();

		// Smaller batch size for dev runs to potentially trigger edge cases
		size_t const batch_size = nano::is_dev_run () ? 2 : 250000;

		auto const total_blocks = backend.count (backend.tx_begin_read (), nano::store::table::blocks);

		size_t processed = 0;
		size_t skipped = 0;

		// View for raw block table iteration (preserves raw bytes for mixed v25/v26 format detection)
		struct raw_blocks_view
		{
			using iterator = nano::store::typed_iterator<nano::block_hash, nano::store::db_val>;

			nano::store::backend & backend;

			iterator begin (nano::store::transaction const & txn, nano::block_hash const & key) const
			{
				return iterator{ backend.begin (txn, nano::store::table::blocks, nano::store::db_val{ key }) };
			}
			iterator end (nano::store::transaction const & txn) const
			{
				return iterator{ backend.end (txn, nano::store::table::blocks) };
			}
		} raw_view{ backend };

		nano::store::crawler crawler{ raw_view, transaction };

		size_t batch_count = 0;
		for (; crawler; ++crawler)
		{
			auto const & [hash, raw_val] = *crawler;

			// Deserialize the block to determine its type and detect sideband format by remaining size
			nano::bufferstream stream{ raw_val.span_view.data (), raw_val.span_view.size () };
			auto block = nano::deserialize_block (stream);
			release_assert (block, "failed to deserialize block during v25->v26 upgrade");

			auto const remaining = static_cast<size_t> (stream.in_avail ());
			auto const expected_v25_size = nano::block_sideband_v25::size (block->type ());
			auto const expected_v26_size = nano::block_sideband::size (block->type ());

			if (remaining == expected_v26_size)
			{
				// Block already in v26 format (from a previous interrupted upgrade), skip
				skipped++;
			}
			else
			{
				release_assert (remaining == expected_v25_size, "unexpected sideband size during v25->v26 upgrade", std::to_string (remaining));

				// Deserialize v25 sideband
				nano::block_sideband_v25 sideband_v25;
				auto error = sideband_v25.deserialize (stream, block->type ());
				release_assert (!error, "failed to deserialize v25 sideband during upgrade");

				// Rewrite block with new sideband format (no successor)
				std::vector<uint8_t> data;
				{
					nano::vectorstream out_stream{ data };
					nano::serialize_block (out_stream, *block);
					nano::block_sideband current_sideband{
						sideband_v25.account,
						sideband_v25.balance,
						sideband_v25.height,
						sideband_v25.timestamp,
						sideband_v25.details,
						sideband_v25.source_epoch
					};
					current_sideband.serialize (out_stream, block->type ());
				}

				nano::store::db_val value{ data.size (), data.data () };
				auto status = backend.put (transaction, nano::store::table::blocks, hash, value);
				backend.release_assert_success (status);

				processed++;
			}

			batch_count++;
			if (batch_count >= batch_size)
			{
				double const percentage = total_blocks > 0 ? (static_cast<double> (processed + skipped) / total_blocks * 100.0) : 0.0;
				logger.info (nano::log::type::ledger_upgrade, "Processed {} blocks ({:.1f}%)", processed + skipped, percentage);

				// Refresh transaction to commit changes
				crawler.refresh ();

				batch_count = 0;
			}
		}

		crawler.refresh ();

		logger.info (nano::log::type::ledger_upgrade, "Done processing {} blocks ({} converted, {} already upgraded)", processed + skipped, processed, skipped);
		version.put (transaction, 26);
	}

	backend.close ();

	logger.info (nano::log::type::ledger_upgrade, "Upgrading database from v25 to v26 completed");
}
}
