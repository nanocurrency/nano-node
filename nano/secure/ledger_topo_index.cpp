#include <nano/lib/block_sideband.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/bounded_dfs.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/topology.hpp>
#include <nano/store/ledger/version.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/meta.hpp>

#include <algorithm>
#include <array>
#include <chrono>

void nano::ledger::populate_topo_index ()
{
	release_assert (!flags.topo_index, "populate_topo_index called when topo_index is already enabled");

	logger.info (nano::log::type::ledger_upgrade, "Populating topology index...");

	uint64_t total_blocks = 0;
	{
		auto txn = tx_begin_read ();
		total_blocks = store.block.count (txn);
	}

	size_t const batch_size_compute = nano::is_dev_run () ? 2 : 1024 * 1024;
	size_t const batch_size_populate = nano::is_dev_run () ? 2 : 16 * 1024 * 1024;
	size_t const max_depth = nano::is_dev_run () ? 16 : 16 * 1024 * 1024;

	// Phase 1: compute topo_height for every block via bounded DFS, write to sideband only
	logger.info (nano::log::type::ledger_upgrade, "Phase 1: computing topology heights for {} blocks...", total_blocks);
	{
		auto txn = tx_begin_write ();
		auto crawler = store.block.crawl (txn);

		size_t resolved = 0;
		auto last_log = std::chrono::steady_clock::now ();

		auto is_resolved = [&] (std::shared_ptr<nano::block> const & blk) -> bool {
			if (!blk)
			{
				return true; // Empty slot (block.dependencies() returned a zero hash)
			}
			return blk->sideband ().topo_height != 0;
		};

		auto get_dependencies = [&] (std::shared_ptr<nano::block> const & blk) -> std::array<std::shared_ptr<nano::block>, 2> {
			auto const dep_hashes = blk->dependencies ();
			std::array<std::shared_ptr<nano::block>, 2> deps{};
			for (size_t i = 0; i < dep_hashes.size (); ++i)
			{
				auto const & dep_hash = dep_hashes[i];
				if (!dep_hash.is_zero ())
				{
					deps[i] = store.block.get (txn, dep_hash);
					release_assert (deps[i], "missing dependency block during topology population", dep_hash.to_string ());
				}
			}
			return deps;
		};

		// 1 + max of all deps' topo heights, minimum 1
		auto compute_topo = [] (std::array<std::shared_ptr<nano::block>, 2> const & deps) -> uint64_t {
			uint64_t topo = 1;
			for (auto const & dep : deps)
			{
				// Empty dep means no dependency
				if (dep)
				{
					release_assert (dep->sideband ().topo_height != 0, "dependency must be resolved before dependent", dep->hash ().to_string ());
					topo = std::max (topo, dep->sideband ().topo_height + 1);
				}
			}
			return topo;
		};

		auto verify_topo = [&] (std::shared_ptr<nano::block> const & blk) {
			auto const topo = blk->sideband ().topo_height;
			auto const deps = get_dependencies (blk);
			for (auto const & dep : deps)
			{
				if (!dep)
				{
					continue; // Empty slot
				}
				release_assert (dep->sideband ().topo_height < topo,
				"topo ordering violation",
				"block " + blk->hash ().to_string () + " topo=" + std::to_string (topo) + " dep " + dep->hash ().to_string () + " dep_topo=" + std::to_string (dep->sideband ().topo_height));
			}
		};

		// The resolved counter counts resolve calls, not unique blocks: diamond deps (e.g. A→X, A→Y→X) yield
		// distinct in-memory shared_ptr copies of X, and is_resolved checks per-instance state,
		// so the same block can be resolved (and overwritten with the same value) more than once.
		// Final progress can therefore exceed total_blocks by a few %.
		auto resolve = [&] (std::shared_ptr<nano::block> const & blk) -> bool {
			// Re-fetch deps to read the updated sidebands
			auto const deps = get_dependencies (blk);
			auto const topo = compute_topo (deps);

			auto const hash = blk->hash ();
			auto sideband = blk->sideband ();
			sideband.topo_height = topo;
			blk->sideband_set (sideband);
			store.block.put (txn, hash, *blk);

			verify_topo (blk);

			++resolved;

			auto now = std::chrono::steady_clock::now ();
			if (now - last_log >= std::chrono::seconds (5))
			{
				logger.info (nano::log::type::ledger_upgrade, "Topology resolve progress: {} / {} blocks ({:.1f}%)", resolved, total_blocks, nano::log::percentage (resolved, total_blocks));
				last_log = now;
			}

			if (resolved % batch_size_compute == 0)
			{
				logger.debug (nano::log::type::ledger_upgrade, "Committing batch of {} blocks...", batch_size_compute);
				auto const refresh_start = std::chrono::steady_clock::now ();

				crawler.refresh ();

				auto const refresh_ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - refresh_start).count ();
				logger.debug (nano::log::type::ledger_upgrade, "Transaction refresh took {}ms", refresh_ms);
			}

			return true;
		};

		size_t processed = 0;
		for (; crawler; ++crawler, ++processed)
		{
			auto const & [hash, bws] = *crawler;

			if (processed % batch_size_compute == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Processing progress: {} / {} blocks ({:.1f}%)", processed, total_blocks, nano::log::percentage (processed, total_blocks));
			}

			if (bws.sideband.topo_height != 0)
			{
				// Already resolved by a previous bounded_dfs call (same phase) or carried over from a prior partial migration
				verify_topo (bws.block);
				continue;
			}

			nano::bounded_dfs_result dfs_result;
			do
			{
				dfs_result = nano::bounded_dfs (bws.block, max_depth, is_resolved, get_dependencies, resolve);
				if (dfs_result.overflow)
				{
					logger.debug (nano::log::type::ledger_upgrade, "Partially resolved {} dependencies for block {}, continuing...", dfs_result.resolved, hash);
				}
			} while (dfs_result.overflow);
		}

		logger.info (nano::log::type::ledger_upgrade, "Resolved {} blocks", resolved);
	}

	// Phase 1 verification: every block must carry a non-zero topo_height with valid ordering against its dependencies
	logger.info (nano::log::type::ledger_upgrade, "Verifying topology heights...");
	{
		auto txn = tx_begin_read ();
		size_t verified = 0;
		for (auto it = store.block.begin (txn), end = store.block.end (txn); it != end; ++it)
		{
			auto const & [hash, bws] = *it;
			auto const & blk = bws.block;
			auto const topo = blk->sideband ().topo_height;
			release_assert (topo != 0, "block missing topo_height after compute phase", hash.to_string ());

			for (auto const & dep_hash : blk->dependencies ())
			{
				if (dep_hash.is_zero ())
				{
					continue;
				}
				auto dep_block = store.block.get (txn, dep_hash);
				release_assert (dep_block, "missing dependency block during topology verification (pruned ledgers are not supported)", dep_hash.to_string ());
				release_assert (dep_block->sideband ().topo_height < topo,
				"topo ordering violation",
				"block " + hash.to_string () + " topo=" + std::to_string (topo) + " dep " + dep_hash.to_string () + " dep_topo=" + std::to_string (dep_block->sideband ().topo_height));
			}

			++verified;

			if (verified % batch_size_compute == 0)
			{
				logger.info (nano::log::type::ledger_upgrade, "Topology verification progress: {} / {} blocks ({:.1f}%)", verified, total_blocks, nano::log::percentage (verified, total_blocks));
			}
		}
	}

	// Phase 2: populate topology table from the topo_height values computed in phase 1
	// Kept separate because random index inserts interleaved with the DFS-heavy phase 1 are very slow
	logger.info (nano::log::type::ledger_upgrade, "Phase 2: populating topology table for {} blocks...", total_blocks);
	{
		// Wipe any rows from a prior partial attempt
		store.topology.clear ();

		auto txn = tx_begin_write ();
		auto crawler = store.block.crawl (txn);

		size_t processed = 0;
		auto last_log = std::chrono::steady_clock::now ();

		while (crawler)
		{
			auto const & [hash, bws] = *crawler;
			auto const topo = bws.sideband.topo_height;
			release_assert (topo != 0, "block missing topo_height during topology table population", hash.to_string ());

			store.topology.put (txn, { topo, hash });

			++processed;

			auto now = std::chrono::steady_clock::now ();
			if (now - last_log >= std::chrono::seconds (5))
			{
				logger.info (nano::log::type::ledger_upgrade, "Topology table progress: {} / {} blocks ({:.1f}%)", processed, total_blocks, nano::log::percentage (processed, total_blocks));
				last_log = now;
			}

			if (processed % batch_size_populate == 0)
			{
				logger.debug (nano::log::type::ledger_upgrade, "Committing batch of {} blocks...", batch_size_populate);
				auto const refresh_start = std::chrono::steady_clock::now ();

				crawler.refresh ();

				auto const refresh_ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - refresh_start).count ();
				logger.debug (nano::log::type::ledger_upgrade, "Transaction refresh took {}ms", refresh_ms);
			}

			++crawler;
		}

		logger.info (nano::log::type::ledger_upgrade, "Done populating topology table for {} blocks", processed);
	}

	// Flip the flag only after both phases commit
	{
		auto txn = tx_begin_write ();
		store.version.put_flag (txn, nano::store::meta_key::topo_index_enabled, true);
	}
	flags.topo_index = true;

	logger.info (nano::log::type::ledger_upgrade, "Topology index populated for {} blocks", total_blocks);
}

void nano::ledger::drop_topo_index ()
{
	release_assert (flags.topo_index, "drop_topo_index called when topo_index is not enabled");

	logger.info (nano::log::type::ledger_upgrade, "Dropping topology index...");

	// Disable the persisted flag first so a crash cannot leave the index marked as enabled after the topology table has been cleared
	{
		auto txn = tx_begin_write ();
		store.version.put_flag (txn, nano::store::meta_key::topo_index_enabled, false);
	}
	store.topology.clear ();

	flags.topo_index = false;

	logger.info (nano::log::type::ledger_upgrade, "Topology index dropped");
}
