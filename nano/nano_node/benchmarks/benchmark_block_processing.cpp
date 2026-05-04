#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/work.hpp>
#include <nano/lib/work_version.hpp>
#include <nano/nano_node/benchmarks/benchmarks.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/unchecked_map.hpp>
#include <nano/store/ledger_store.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_set>

#include <fmt/format.h>

namespace nano::cli
{
/*
 * Block Processing Benchmark
 *
 * Measures the performance of the block processor - the component responsible for validating
 * and inserting blocks into the ledger. This benchmark tests raw block processing throughput
 * without elections or confirmation.
 *
 * How it works:
 * 1. Setup: Creates a node with unlimited queue sizes and disabled work requirements
 * 2. Generate: Creates random transfer transactions (send/receive pairs) between accounts
 * 3. Submit: Adds all blocks to the block processor queue via block_processor.add()
 * 4. Measure: Tracks time from submission until all blocks are processed into the ledger
 * 5. Report: Calculates blocks/sec throughput and final account states
 *
 * What is tested:
 * - Block validation speed (signature verification, balance checks, etc.)
 * - Ledger write performance (database insertion)
 * - Block processor queue management
 * - Unchecked block handling for out-of-order blocks
 *
 * What is NOT tested:
 * - Elections or voting (blocks are not confirmed)
 * - Cementing (blocks remain unconfirmed)
 * - Network communication (local-only testing)
 */
class block_processing_benchmark : public benchmark_base
{
private:
	// Blocks currently being processed
	nano::locked<std::unordered_set<nano::block_hash>> current_blocks;

	// Metrics
	std::atomic<size_t> processed_blocks_count{ 0 };
	std::atomic<size_t> failed_blocks_count{ 0 };
	std::atomic<size_t> old_blocks_count{ 0 };
	std::atomic<size_t> gap_previous_count{ 0 };
	std::atomic<size_t> gap_source_count{ 0 };

public:
	block_processing_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a);

	void run ();
	void run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks);
	void print_statistics ();
};

void run_block_processing_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path)
{
	auto config = benchmark_config::parse (vm);

	std::cout << "=== BENCHMARK: Block Processing ===\n";
	std::cout << "Configuration:\n";
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

	std::cout << "\nSystem Info:\n";
	fmt::print ("  Backend: {}\n", node->store.get_vendor ());
	fmt::print ("  Block processor threads: {}\n", 1); // TODO: Log number of block processor threads when upstreamed
	fmt::print ("  Block processor batch size: {}\n", node->config.block_processor->batch_size);
	std::cout << "\n";

	// Wait for node to be ready
	std::this_thread::sleep_for (500ms);

	// Run benchmark
	block_processing_benchmark benchmark{ node, config };
	benchmark.run ();

	node->stop ();
}

block_processing_benchmark::block_processing_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a) :
	benchmark_base (node_a, config_a)
{
	// Register notification handler to track block processing results
	node->ledger_notifications.blocks_processed.add ([this] (std::deque<std::pair<nano::block_status, nano::block_context>> const & batch) {
		auto current_l = current_blocks.lock ();
		for (auto const & [status, context] : batch)
		{
			if (status == nano::block_status::progress)
			{
				current_l->erase (context.block->hash ());
				processed_blocks_count++;
			}
			else
			{
				switch (status)
				{
					case nano::block_status::old:
						// Block already exists in ledger
						old_blocks_count++;
						break;
					case nano::block_status::gap_previous:
						// Missing previous block, should be handled by unchecked map
						gap_previous_count++;
						break;
					case nano::block_status::gap_source:
						// Missing source block, should be handled by unchecked map
						gap_source_count++;
						break;
					default:
						fmt::print ("Block processing failed: {} for block {}\n", to_string (status), context.block->hash ().to_string ());
						failed_blocks_count++;
						break;
				}
			}
		}
	});
}

void block_processing_benchmark::run ()
{
	// Create account pool and distribute genesis funds to a random account
	fmt::print ("Generating {} accounts...\n", config.num_accounts);
	pool.generate_accounts (config.num_accounts);

	setup_genesis_distribution ();

	// Run multiple iterations to measure consistent performance
	for (size_t iteration = 0; iteration < config.num_iterations; ++iteration)
	{
		fmt::print ("\n--- Iteration {}/{} --------------------------------------------------------------\n", iteration + 1, config.num_iterations);
		fmt::print ("Generating {} random transfers...\n", config.batch_size / 2);
		auto blocks = generate_random_transfers ();

		fmt::print ("Processing {} blocks...\n", blocks.size ());
		run_iteration (blocks);
	}

	print_statistics ();
}

void block_processing_benchmark::run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks)
{
	auto const total_blocks = blocks.size ();

	// Add all blocks to tracking set
	{
		auto current_l = current_blocks.lock ();
		for (auto const & block : blocks)
		{
			current_l->insert (block->hash ());
		}
	}

	auto const time_begin = std::chrono::high_resolution_clock::now ();

	// Process all blocks
	auto added = node->block_processor.add_many (blocks, nano::block_source::test);
	release_assert (added == blocks.size (), "failed to add all blocks to processor");
	blocks.clear ();

	// Wait for processing to complete
	nano::interval progress_interval;
	while (true)
	{
		{
			auto current_l = current_blocks.lock ();
			if (current_l->empty () || progress_interval.elapse (3s))
			{
				fmt::print ("Blocks remaining: {:>9} (block processor: {:>9} | unchecked: {:>5})\n",
				current_l->size (),
				node->block_processor.size (),
				node->unchecked.count ());
			}
			if (current_l->empty ())
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
	std::cout << "─────────────────────────────────────────────────────────────────\n";

	node->stats.clear ();
}

void block_processing_benchmark::print_statistics ()
{
	std::cout << "\n--- SUMMARY ---------------------------------------------------------------------\n\n";
	fmt::print ("Blocks processed:        {:>10}\n", processed_blocks_count.load ());
	fmt::print ("Blocks failed:           {:>10}\n", failed_blocks_count.load ());
	fmt::print ("Blocks old:              {:>10}\n", old_blocks_count.load ());
	fmt::print ("Blocks gap_previous:     {:>10}\n", gap_previous_count.load ());
	fmt::print ("Blocks gap_source:       {:>10}\n", gap_source_count.load ());
	fmt::print ("\n");
	fmt::print ("Accounts total:          {:>10}\n", pool.total_accounts ());
	fmt::print ("Accounts with balance:   {:>10} ({:.1f}%)\n",
	pool.accounts_with_balance_count (),
	100.0 * pool.accounts_with_balance_count () / pool.total_accounts ());
}
}
