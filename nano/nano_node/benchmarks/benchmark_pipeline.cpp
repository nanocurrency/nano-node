#include <nano/lib/config.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/timer.hpp>
#include <nano/nano_node/benchmarks/benchmarks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/cli.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/node/election.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/scheduler/component.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include <fmt/format.h>

namespace nano::cli
{
/*
 * Full Pipeline Benchmark
 *
 * Measures the complete block confirmation pipeline from submission through processing,
 * elections, and cementing. Tests all stages together including inter-component coordination.
 *
 * How it works:
 * 1. Setup: Creates a node with genesis representative key for voting
 * 2. Generate: Creates random transfer transactions (send/receive pairs)
 * 3. Submit: Adds blocks via process_active() which triggers the full pipeline
 * 4. Measure: Tracks time from submission through processing, election, and cementing
 * 5. Report: Calculates overall throughput and timing breakdown for each stage
 *
 * Pipeline stages measured:
 * - Block processing: submission -> ledger insertion
 * - Election activation: ledger insertion -> election start
 * - Election confirmation: election start -> block cemented
 * - Total pipeline: submission -> cemented
 *
 * What is tested:
 * - Block processor throughput
 * - Election startup and scheduling
 * - Vote generation and processing (with one local rep)
 * - Quorum detection and confirmation
 * - Cementing performance
 * - Inter-component coordination and queueing
 *
 * What is NOT tested:
 * - Network communication (local-only)
 * - Multiple remote representatives
 */
class pipeline_benchmark : public benchmark_base
{
private:
	struct block_timing
	{
		std::chrono::steady_clock::time_point submitted;
		std::chrono::steady_clock::time_point processed;
		std::chrono::steady_clock::time_point election_started;
		std::chrono::steady_clock::time_point election_stopped;
		std::chrono::steady_clock::time_point confirmed;
		std::chrono::steady_clock::time_point cemented;
	};

	// Track timing for each block through the pipeline
	nano::locked<std::unordered_map<nano::block_hash, block_timing>> block_timings;

	// Track blocks waiting to be cemented
	nano::locked<std::unordered_map<nano::block_hash, std::chrono::steady_clock::time_point>> pending_cementing;

	// Metrics
	std::atomic<size_t> elections_started{ 0 };
	std::atomic<size_t> elections_stopped{ 0 };
	std::atomic<size_t> elections_confirmed{ 0 };
	std::atomic<size_t> blocks_cemented{ 0 };

public:
	pipeline_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a);

	void run ();
	void run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks);
	void print_statistics ();
};

void run_pipeline_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path)
{
	auto config = benchmark_config::parse (vm);

	std::cout << "=== BENCHMARK: Full Pipeline ===\n";
	std::cout << "Configuration:\n";
	std::cout << fmt::format ("  Accounts: {}\n", config.num_accounts);
	std::cout << fmt::format ("  Iterations: {}\n", config.num_iterations);
	std::cout << fmt::format ("  Batch size: {}\n", config.batch_size);

	// Setup node directly in run method
	nano::set_active_network (nano::network_type::nano_dev_network);
	nano::logger::initialize (nano::log_config::cli_default (nano::log::level::warn));

	nano::node_flags node_flags;
	nano::update_flags (node_flags, vm);

	auto io_ctx = std::make_shared<boost::asio::io_context> ();
	nano::work_pool work_pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	// Load configuration from current working directory (if exists) and cli config overrides
	auto daemon_config = nano::load_config_file<nano::daemon_config> (nano::node_config_filename, {}, node_flags.config_overrides);
	auto node_config = daemon_config.node;
	node_config.network_params.work = nano::work_thresholds{ 0, 0, 0 };
	node_config.peering_port = 0; // Use random available port
	node_config.max_backlog = 0; // Disable bounded backlog
	node_config.block_processor.max_peer_queue = std::numeric_limits<size_t>::max (); // Unlimited queue size
	node_config.block_processor.max_system_queue = std::numeric_limits<size_t>::max (); // Unlimited queue size
	node_config.max_unchecked_blocks = 1024 * 1024; // Large unchecked blocks cache to avoid dropping blocks
	node_config.vote_processor.max_pr_queue = std::numeric_limits<size_t>::max (); // Unlimited vote processing queue

	node_config.priority_bucket.max_blocks = std::numeric_limits<size_t>::max (); // Unlimited priority bucket
	node_config.priority_bucket.max_elections = std::numeric_limits<size_t>::max (); // Unlimited bucket elections
	node_config.priority_bucket.reserved_elections = std::numeric_limits<size_t>::max (); // Unlimited bucket elections

	auto node = std::make_shared<nano::node> (io_ctx, nano::unique_path (), node_config, work_pool, node_flags);
	node->start ();
	nano::thread_runner runner (io_ctx, nano::default_logger (), node->config.io_threads);

	std::cout << "\nSystem Info:\n";
	std::cout << fmt::format ("  Backend: {}\n", node->store.vendor_get ());
	std::cout << fmt::format ("  Block processor threads: {}\n", 1); // TODO: Log number of block processor threads when upstreamed
	std::cout << fmt::format ("  Vote processor threads: {}\n", node->config.vote_processor.threads);
	std::cout << fmt::format ("  Active elections limit: {}\n", node->config.active_elections.size);
	std::cout << fmt::format ("  Priority bucket max blocks: {}\n", node->config.priority_bucket.max_blocks);
	std::cout << fmt::format ("  Priority bucket max elections: {}\n", node->config.priority_bucket.max_elections);
	std::cout << fmt::format ("  Block processor max peer queue: {}\n", node->config.block_processor.max_peer_queue);
	std::cout << fmt::format ("  Block processor max system queue: {}\n", node->config.block_processor.max_system_queue);
	std::cout << fmt::format ("  Vote processor max pr queue: {}\n", node->config.vote_processor.max_pr_queue);
	std::cout << fmt::format ("  Max unchecked blocks: {}\n", node->config.max_unchecked_blocks);
	std::cout << "\n";

	// Insert dev genesis representative key for voting
	auto wallet = node->wallets.create (nano::random_wallet_id ());
	wallet->insert_adhoc (nano::dev::genesis_key.prv);

	// Wait for node to be ready
	std::this_thread::sleep_for (500ms);

	// Run benchmark
	pipeline_benchmark benchmark{ node, config };
	benchmark.run ();

	node->stop ();
}

pipeline_benchmark::pipeline_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a) :
	benchmark_base (node_a, config_a)
{
	// Track when blocks get processed
	node->ledger_notifications.blocks_processed.add ([this] (std::deque<std::pair<nano::block_status, nano::block_context>> const & batch) {
		auto now = std::chrono::steady_clock::now ();
		auto timings_l = block_timings.lock ();

		for (auto const & [status, context] : batch)
		{
			if (status == nano::block_status::progress)
			{
				if (auto it = timings_l->find (context.block->hash ()); it != timings_l->end ())
				{
					it->second.processed = now;
				}
				processed_blocks_count++;
			}
		}
	});

	// Track when elections start
	node->active.election_started.add ([this] (std::shared_ptr<nano::election> const & election, nano::bucket_index const & bucket, nano::priority_timestamp const & priority) {
		auto now = std::chrono::steady_clock::now ();
		auto hash = election->winner ()->hash ();
		auto timings_l = block_timings.lock ();

		if (auto it = timings_l->find (hash); it != timings_l->end ())
		{
			it->second.election_started = now;
		}

		elections_started++;
	});

	// Track when elections stop (regardless of confirmation)
	node->active.election_erased.add ([this] (std::shared_ptr<nano::election> const & election) {
		auto now = std::chrono::steady_clock::now ();
		auto hash = election->winner ()->hash ();
		auto timings_l = block_timings.lock ();

		if (auto it = timings_l->find (hash); it != timings_l->end ())
		{
			it->second.election_stopped = now;
		}

		elections_stopped++;
		elections_confirmed += election->confirmed () ? 1 : 0;
	});

	// Track when blocks get cemented
	node->cementing_set.batch_cemented.add ([this] (auto const & hashes) {
		auto now = std::chrono::steady_clock::now ();
		auto pending_l = pending_cementing.lock ();
		auto timings_l = block_timings.lock ();

		for (auto const & ctx : hashes)
		{
			auto hash = ctx.block->hash ();

			if (auto it = timings_l->find (hash); it != timings_l->end ())
			{
				it->second.cemented = now;
			}
			pending_l->erase (hash);

			blocks_cemented++;
		}
	});
}

void pipeline_benchmark::run ()
{
	std::cout << fmt::format ("Generating {} accounts...\n", config.num_accounts);
	pool.generate_accounts (config.num_accounts);

	setup_genesis_distribution (0.1); // Only distribute 10%, keep 90% for voting weight

	for (size_t iteration = 0; iteration < config.num_iterations; ++iteration)
	{
		std::cout << fmt::format ("\n--- Iteration {}/{} --------------------------------------------------------------\n", iteration + 1, config.num_iterations);
		std::cout << fmt::format ("Generating {} random transfers...\n", config.batch_size / 2);
		auto blocks = generate_random_transfers ();

		std::cout << fmt::format ("Measuring full confirmation pipeline for {} blocks...\n", blocks.size ());
		run_iteration (blocks);
	}

	print_statistics ();
}

void pipeline_benchmark::run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks)
{
	auto const total_blocks = blocks.size ();

	// Initialize timing entries for all blocks
	{
		auto now = std::chrono::steady_clock::now ();
		auto timings_l = block_timings.lock ();
		auto pending_l = pending_cementing.lock ();
		for (auto const & block : blocks)
		{
			timings_l->emplace (block->hash (), block_timing{ now });
			pending_l->emplace (block->hash (), now);
		}
	}

	auto const time_begin = std::chrono::high_resolution_clock::now ();

	// Submit all blocks through the full pipeline
	while (!blocks.empty ())
	{
		auto block = blocks.front ();
		blocks.pop_front ();

		// Process block through full confirmation pipeline
		node->process_active (block);
	}

	// Wait for all blocks to be confirmed and cemented
	nano::interval progress_interval;
	while (true)
	{
		{
			auto pending_l = pending_cementing.lock ();
			if (pending_l->empty () || progress_interval.elapse (3s))
			{
				std::cout << fmt::format ("Blocks remaining: {:>9} (block processor: {:>9} | active: {:>5} | cementing: {:>5} | pool: {:>5})\n",
				pending_l->size (),
				node->block_processor.size (),
				node->active.size (),
				node->cementing_set.size (),
				node->scheduler.priority.size ());
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

	std::cout << fmt::format ("\nPerformance: {} blocks/sec [{:.2f}s] {} blocks processed\n",
	total_blocks * 1000000 / time_us, time_us / 1000000.0, total_blocks);
	std::cout << "─────────────────────────────────────────────────────────────────\n";

	node->stats.clear ();
}

void pipeline_benchmark::print_statistics ()
{
	std::cout << "\n--- SUMMARY ---------------------------------------------------------------------\n\n";
	std::cout << fmt::format ("Blocks processed:        {:>10}\n", processed_blocks_count.load ());
	std::cout << fmt::format ("Elections started:       {:>10}\n", elections_started.load ());
	std::cout << fmt::format ("Elections stopped:       {:>10}\n", elections_stopped.load ());
	std::cout << fmt::format ("Elections confirmed:     {:>10}\n", elections_confirmed.load ());
	std::cout << fmt::format ("\n");
	std::cout << fmt::format ("Accounts total:          {:>10}\n", pool.total_accounts ());
	std::cout << fmt::format ("Accounts with balance:   {:>10} ({:.1f}%)\n",
	pool.accounts_with_balance_count (),
	100.0 * pool.accounts_with_balance_count () / pool.total_accounts ());

	// Calculate timing statistics from raw data
	auto timings_l = block_timings.lock ();

	uint64_t total_processing_time = 0;
	uint64_t total_activation_time = 0;
	uint64_t total_election_time = 0;
	uint64_t total_cementing_time = 0;
	size_t processed_count = 0;
	size_t activation_count = 0;
	size_t election_count = 0;
	size_t cemented_count = 0;

	for (auto const & [hash, timing] : *timings_l)
	{
		release_assert (timing.submitted != std::chrono::steady_clock::time_point{});
		release_assert (timing.election_started != std::chrono::steady_clock::time_point{});
		release_assert (timing.election_stopped != std::chrono::steady_clock::time_point{});
		release_assert (timing.cemented != std::chrono::steady_clock::time_point{});

		total_processing_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.processed - timing.submitted).count ();
		processed_count++;

		total_activation_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.election_started - timing.processed).count ();
		activation_count++;

		total_election_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.cemented - timing.election_started).count ();
		election_count++;

		total_cementing_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.cemented - timing.submitted).count ();
		cemented_count++;
	}

	std::cout << "\n";
	std::cout << fmt::format ("Block processing (submitted > processed):    {:>8.2f} ms/block avg\n", total_processing_time / (processed_count * 1000.0));
	std::cout << fmt::format ("Election activation (processed > activated): {:>8.2f} ms/block avg\n", total_activation_time / (activation_count * 1000.0));
	std::cout << fmt::format ("Election time (activated > confirmed):       {:>8.2f} ms/block avg\n", total_election_time / (election_count * 1000.0));
	std::cout << fmt::format ("Total pipeline (submitted > cemented):       {:>8.2f} ms/block avg\n", total_cementing_time / (cemented_count * 1000.0));
}
}