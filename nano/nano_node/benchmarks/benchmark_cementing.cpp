#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/work.hpp>
#include <nano/nano_node/benchmarks/benchmarks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>

#include <fmt/format.h>

namespace nano::cli
{
/*
 * Cementing Benchmark
 *
 * Measures the performance of the cementing subsystem - the component that marks blocks
 * as confirmed/immutable in the ledger.
 *
 * How it works:
 * 1. Setup: Creates a node and generates random transfer blocks
 * 2. Process: Inserts blocks directly into ledger (bypassing block processor)
 * 3. Submit: Adds blocks to cementing set for confirmation
 * 4. Measure: Tracks time from submission until all blocks are cemented
 * 5. Report: Calculates cementing throughput in blocks/sec
 *
 * Two modes:
 * - Sequential mode: Each block is submitted to cementing set individually
 * - Root mode: Only the final block is submitted, which triggers cascading cementing
 *              of all dependent blocks (tests dependency resolution performance)
 *
 * What is tested:
 * - Cementing set processing speed
 * - Database write performance for confirmation marks
 * - Dependency resolution (root mode only)
 *
 * What is NOT tested:
 * - Block processing (blocks inserted directly into ledger)
 * - Elections or voting (blocks pre-confirmed)
 * - Network communication
 */
class cementing_benchmark : public benchmark_base
{
private:
	// Track blocks waiting to be cemented
	nano::locked<std::unordered_map<nano::block_hash, std::chrono::steady_clock::time_point>> pending_cementing;

	// Metrics
	std::atomic<size_t> processed_blocks_count{ 0 };
	std::atomic<size_t> cemented_blocks_count{ 0 };

public:
	cementing_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a);

	void run ();
	void run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks);
	void print_statistics ();
};

void run_cementing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path)
{
	auto config = benchmark_config::parse (vm);

	fmt::print ("=== BENCHMARK: Cementing ===\n");
	fmt::print ("Configuration:\n");
	fmt::print ("  Mode: {}\n", config.cementing_mode == cementing_mode::root ? "root" : "sequential");
	fmt::print ("  Accounts: {}\n", config.num_accounts);
	fmt::print ("  Iterations: {}\n", config.num_iterations);
	fmt::print ("  Batch size: {}\n", config.batch_size);

	// Setup node directly in run method
	nano::set_active_network (nano::network_type::nano_dev_network);
	nano::logger::initialize (nano::log_config::cli_default (nano::log::level::warn));

	nano::node_flags node_flags;
	nano::update_flags (node_flags, vm);

	nano::work_pool work_pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	// Load configuration from current working directory (if exists) and cli config overrides
	auto daemon_config = nano::load_config_file<nano::daemon_config> (nano::node_config_filename, {}, node_flags.config_overrides);
	auto node_config = daemon_config.node;
	node_config.network_params.work = nano::work_thresholds{ 0, 0, 0 };
	node_config.peering_port = 0; // Use random available port
	node_config.max_backlog = 0; // Disable bounded backlog
	node_config.block_processor->max_system_queue = std::numeric_limits<size_t>::max (); // Unlimited queue size
	node_config.max_unchecked_blocks = 1024 * 1024; // Large unchecked blocks cache to avoid dropping blocks

	auto node = std::make_shared<nano::node> (nano::unique_path (), node_config, work_pool, node_flags);
	node->start ();

	fmt::print ("\nSystem Info:\n");
	fmt::print ("  Backend: {}\n", node->store.get_vendor ());
	fmt::print ("\n");

	// Wait for node to be ready
	std::this_thread::sleep_for (500ms);

	// Run benchmark
	cementing_benchmark benchmark{ node, config };
	benchmark.run ();

	node->stop ();
}

cementing_benchmark::cementing_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a) :
	benchmark_base (node_a, config_a)
{
	// Track when blocks get processed
	node->ledger_notifications.blocks_processed.add ([this] (std::deque<std::pair<nano::block_status, nano::block_context>> const & batch) {
		for (auto const & [status, context] : batch)
		{
			if (status == nano::block_status::progress)
			{
				processed_blocks_count++;
			}
		}
	});

	// Track when blocks get cemented
	node->cementing_set.batch_cemented.add ([this] (auto const & hashes) {
		auto pending_l = pending_cementing.lock ();
		for (auto const & ctx : hashes)
		{
			pending_l->erase (ctx.block->hash ());
			cemented_blocks_count++;
		}
	});
}

void cementing_benchmark::run ()
{
	fmt::print ("Generating {} accounts...\n", config.num_accounts);
	pool.generate_accounts (config.num_accounts);

	setup_genesis_distribution ();

	fmt::print ("Cementing mode: {}\n", config.cementing_mode == cementing_mode::root ? "root" : "sequential");

	for (size_t iteration = 0; iteration < config.num_iterations; ++iteration)
	{
		fmt::print ("\n--- Iteration {}/{} --------------------------------------------------------------\n", iteration + 1, config.num_iterations);

		std::deque<std::shared_ptr<nano::block>> blocks;
		if (config.cementing_mode == cementing_mode::root)
		{
			fmt::print ("Generating dependent chain topology...\n");
			blocks = generate_dependent_chain ();
		}
		else
		{
			fmt::print ("Generating {} random transfers...\n", config.batch_size / 2);
			blocks = generate_random_transfers ();
		}

		fmt::print ("Cementing {} blocks...\n", blocks.size ());
		run_iteration (blocks);
	}

	print_statistics ();
}

void cementing_benchmark::run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks)
{
	auto const total_blocks = blocks.size ();

	// Add all blocks to tracking set
	{
		auto now = std::chrono::steady_clock::now ();
		auto pending_l = pending_cementing.lock ();
		for (auto const & block : blocks)
		{
			pending_l->emplace (block->hash (), now);
		}
	}

	fmt::print ("Processing {} blocks directly into the ledger...\n", blocks.size ());

	// Process all blocks directly into the ledger
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto const & block : blocks)
		{
			auto result = node->ledger.process (transaction, block);
			release_assert (result == nano::block_status::progress, to_string (result));
		}
	}

	fmt::print ("All blocks processed, starting cementing...\n");

	auto const time_begin = std::chrono::high_resolution_clock::now ();

	// Mode-specific cementing
	size_t blocks_submitted = 0;
	if (config.cementing_mode == cementing_mode::root)
	{
		// In root mode, only submit the final block which depends on all others
		if (!blocks.empty ())
		{
			auto final_block = blocks.back ();
			bool added = node->cementing_set.add (final_block->hash ());
			release_assert (added, "failed to add final block to cementing set");
			blocks_submitted = 1;

			fmt::print ("Submitted 1 root block to cement {} dependent blocks\n",
			total_blocks);
		}
	}
	else
	{
		// Sequential mode - submit each block separately
		while (!blocks.empty ())
		{
			auto block = blocks.front ();
			blocks.pop_front ();

			bool added = node->cementing_set.add (block->hash ());
			release_assert (added, "failed to add block to cementing set");
			blocks_submitted++;
		}

		fmt::print ("Submitted {} blocks to cementing set\n",
		blocks_submitted);
	}

	// Wait for cementing to complete
	nano::interval progress_interval;
	while (true)
	{
		{
			auto pending_l = pending_cementing.lock ();
			if (pending_l->empty () || progress_interval.elapse (3s))
			{
				fmt::print ("Blocks remaining: {:>9} (cementing set: {:>5} | deferred: {:>5})\n",
				pending_l->size (),
				node->cementing_set.size (),
				node->cementing_set.deferred_size ());
			}
			if (pending_l->empty ())
			{
				break;
			}
		}

		std::this_thread::sleep_for (1ms);
	}

	auto const time_end = std::chrono::high_resolution_clock::now ();
	auto const time_us = std::chrono::duration_cast<std::chrono::microseconds> (time_end - time_begin).count ();

	fmt::print ("\nPerformance: {} blocks/sec [{:.2f}s] {} blocks processed\n",
	total_blocks * 1000000 / time_us, time_us / 1000000.0, total_blocks);
	fmt::print ("─────────────────────────────────────────────────────────────────\n");

	node->stats.clear ();
}

void cementing_benchmark::print_statistics ()
{
	fmt::print ("\n--- SUMMARY ---------------------------------------------------------------------\n\n");
	fmt::print ("Mode:                    {:>10}\n", config.cementing_mode == cementing_mode::root ? "root" : "sequential");
	fmt::print ("Blocks processed:        {:>10}\n", processed_blocks_count.load ());
	fmt::print ("Blocks cemented:         {:>10}\n", cemented_blocks_count.load ());
	fmt::print ("\n");
	fmt::print ("Accounts total:          {:>10}\n", pool.total_accounts ());
	fmt::print ("Accounts with balance:   {:>10} ({:.1f}%)\n",
	pool.accounts_with_balance_count (),
	100.0 * pool.accounts_with_balance_count () / pool.total_accounts ());
}
}
