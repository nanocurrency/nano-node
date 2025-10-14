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
#include <nano/node/scheduler/manual.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include <fmt/format.h>

namespace nano::cli
{
/*
 * Elections Benchmark
 *
 * Measures the performance of the election subsystem - the component that runs voting
 * consensus to cement blocks. Tests how quickly the node can start elections, collect
 * votes, reach quorum, and cement blocks.
 *
 * How it works:
 * 1. Setup: Creates a node with genesis representative key for voting
 * 2. Prepare: Generates independent open blocks (send blocks are pre-cemented)
 * 3. Process: Inserts open blocks directly into ledger (bypassing block processor)
 * 4. Start: Manually triggers elections for all open blocks
 * 5. Measure: Tracks time from election start until blocks are confirmed and cemented
 * 6. Report: Calculates election throughput and timing statistics
 *
 * What is tested:
 * - Election startup performance
 * - Vote generation and processing speed (with one local rep running on the same node)
 * - Quorum detection and confirmation logic
 * - Cementing after confirmation
 * - Concurrent election handling
 *
 * What is NOT tested:
 * - Block processing (blocks inserted directly)
 * - Network vote propagation (local voting only)
 * - Election schedulers (elections started manually)
 */
class elections_benchmark : public benchmark_base
{
private:
	struct block_timing
	{
		std::chrono::steady_clock::time_point submitted;
		std::chrono::steady_clock::time_point election_started;
		std::chrono::steady_clock::time_point election_stopped;
		std::chrono::steady_clock::time_point cemented;
	};

	// Track timing for each block through the election pipeline
	nano::locked<std::unordered_map<nano::block_hash, block_timing>> block_timings;

	nano::locked<std::unordered_set<nano::block_hash>> pending_confirmation;
	nano::locked<std::unordered_set<nano::block_hash>> pending_cementing;

	// Metrics
	std::atomic<size_t> elections_started{ 0 };
	std::atomic<size_t> elections_stopped{ 0 };
	std::atomic<size_t> elections_confirmed{ 0 };
	std::atomic<size_t> blocks_cemented{ 0 };

public:
	elections_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a);

	void run ();
	void run_iteration (std::deque<std::shared_ptr<nano::block>> & sends, std::deque<std::shared_ptr<nano::block>> & opens);
	void print_statistics ();
};

void run_elections_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path)
{
	auto config = benchmark_config::parse (vm);

	std::cout << "=== BENCHMARK: Elections ===\n";
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

	// Disable election schedulers and backlog scanning
	node_config.hinted_scheduler.enable = false;
	node_config.optimistic_scheduler.enable = false;
	node_config.priority_scheduler.enable = false;
	node_config.backlog_scan.enable = false;

	node_config.block_processor.max_peer_queue = std::numeric_limits<size_t>::max (); // Unlimited queue size
	node_config.block_processor.max_system_queue = std::numeric_limits<size_t>::max (); // Unlimited queue size
	node_config.max_unchecked_blocks = 1024 * 1024; // Large unchecked blocks cache to avoid dropping blocks
	node_config.vote_processor.max_pr_queue = std::numeric_limits<size_t>::max (); // Unlimited vote processing queue

	auto node = std::make_shared<nano::node> (io_ctx, nano::unique_path (), node_config, work_pool, node_flags);
	node->start ();
	nano::thread_runner runner (io_ctx, nano::default_logger (), node->config.io_threads);

	std::cout << "\nSystem Info:\n";
	std::cout << fmt::format ("  Backend: {}\n", node->store.vendor_get ());
	std::cout << "\n";

	// Insert dev genesis representative key for voting
	auto wallet = node->wallets.create (nano::random_wallet_id ());
	wallet->insert_adhoc (nano::dev::genesis_key.prv);

	// Wait for node to be ready
	std::this_thread::sleep_for (500ms);

	// Run benchmark
	elections_benchmark benchmark{ node, config };
	benchmark.run ();

	node->stop ();
}

elections_benchmark::elections_benchmark (std::shared_ptr<nano::node> node_a, benchmark_config const & config_a) :
	benchmark_base (node_a, config_a)
{
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
		auto pending_confirmation_l = pending_confirmation.lock ();

		if (auto it = timings_l->find (hash); it != timings_l->end ())
		{
			it->second.election_stopped = now;
		}
		pending_confirmation_l->erase (hash);

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

void elections_benchmark::run ()
{
	std::cout << fmt::format ("Generating {} accounts...\n", config.num_accounts);
	pool.generate_accounts (config.num_accounts);

	setup_genesis_distribution (0.1); // Only distribute 10%, keep 90% for voting weight

	for (size_t iteration = 0; iteration < config.num_iterations; ++iteration)
	{
		std::cout << fmt::format ("\n--- Iteration {}/{} --------------------------------------------------------------\n", iteration + 1, config.num_iterations);
		std::cout << fmt::format ("Generating independent blocks...\n");
		auto [sends, opens] = generate_independent_blocks ();

		std::cout << fmt::format ("Measuring elections performance for {} opens...\n", opens.size ());
		run_iteration (sends, opens);
	}

	print_statistics ();
}

void elections_benchmark::run_iteration (std::deque<std::shared_ptr<nano::block>> & sends, std::deque<std::shared_ptr<nano::block>> & opens)
{
	auto const total_opens = opens.size ();

	// Process and cement all send blocks directly
	std::cout << fmt::format ("Processing and cementing {} send blocks...\n", sends.size ());
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto const & send : sends)
		{
			auto result = node->ledger.process (transaction, send);
			release_assert (result == nano::block_status::progress, to_string (result));

			// Add to cementing set for direct cementing
			auto cemented = node->ledger.confirm (transaction, send->hash ());
			release_assert (!cemented.empty () && cemented.back ()->hash () == send->hash ());
		}
	}

	// Process open blocks into ledger without confirming
	std::cout << fmt::format ("Processing {} open blocks into ledger...\n", opens.size ());
	{
		auto transaction = node->ledger.tx_begin_write ();
		for (auto const & open : opens)
		{
			auto result = node->ledger.process (transaction, open);
			release_assert (result == nano::block_status::progress, to_string (result));
		}
	}

	// Initialize timing entries for open blocks only
	{
		auto now = std::chrono::steady_clock::now ();
		auto timings_l = block_timings.lock ();
		auto pending_cementing_l = pending_cementing.lock ();
		auto pending_confirmation_l = pending_confirmation.lock ();
		for (auto const & open : opens)
		{
			pending_cementing_l->emplace (open->hash ());
			pending_confirmation_l->emplace (open->hash ());
			timings_l->emplace (open->hash (), block_timing{ now });
		}
	}

	auto const time_begin = std::chrono::high_resolution_clock::now ();

	// Manually start elections for open blocks only
	std::cout << fmt::format ("Starting elections manually for {} open blocks...\n", opens.size ());
	for (auto const & open : opens)
	{
		// Use manual scheduler to start election
		node->scheduler.manual.push (open);
	}

	// Wait for all elections to complete and blocks to be cemented
	nano::interval progress_interval;
	while (true)
	{
		{
			auto pending_cementing_l = pending_cementing.lock ();
			auto pending_confirmation_l = pending_confirmation.lock ();

			if ((pending_cementing_l->empty () && pending_confirmation_l->empty ()) || progress_interval.elapse (3s))
			{
				std::cout << fmt::format ("Confirming elections: {:>9} remaining | cementing: {:>9} remaining (active: {:>5} | cementing: {:>5} | deferred: {:>5})\n",
				pending_confirmation_l->size (),
				pending_cementing_l->size (),
				node->active.size (),
				node->cementing_set.size (),
				node->cementing_set.deferred_size ());
			}
			if (pending_cementing_l->empty () && pending_confirmation_l->empty ())
			{
				break;
			}
		}

		std::this_thread::sleep_for (1ms);
	}

	auto const time_end = std::chrono::high_resolution_clock::now ();
	auto const time_us = std::chrono::duration_cast<std::chrono::microseconds> (time_end - time_begin).count ();

	std::cout << fmt::format ("\nPerformance: {} blocks/sec [{:.2f}s] {} blocks processed\n",
	total_opens * 1000000 / time_us, time_us / 1000000.0, total_opens);
	std::cout << "─────────────────────────────────────────────────────────────────\n";

	node->stats.clear ();
}

void elections_benchmark::print_statistics ()
{
	std::cout << "\n--- SUMMARY ---------------------------------------------------------------------\n\n";
	std::cout << fmt::format ("Elections started:       {:>10}\n", elections_started.load ());
	std::cout << fmt::format ("Elections stopped:       {:>10}\n", elections_stopped.load ());
	std::cout << fmt::format ("Elections confirmed:     {:>10}\n", elections_confirmed.load ());
	std::cout << fmt::format ("\n");

	// Calculate timing statistics from raw data
	auto timings_l = block_timings.lock ();

	uint64_t total_election_time = 0;
	uint64_t total_confirmation_time = 0;
	size_t election_count = 0;
	size_t confirmed_count = 0;

	for (auto const & [hash, timing] : *timings_l)
	{
		release_assert (timing.election_started != std::chrono::steady_clock::time_point{});
		release_assert (timing.election_stopped != std::chrono::steady_clock::time_point{});
		release_assert (timing.cemented != std::chrono::steady_clock::time_point{});

		total_election_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.election_stopped - timing.election_started).count ();
		election_count++;

		total_confirmation_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.cemented - timing.election_started).count ();
		confirmed_count++;
	}

	std::cout << "\n";

	std::cout << fmt::format ("Election time (activated > confirmed): {:>8.2f} ms/block avg\n", total_election_time / (election_count * 1000.0));
	std::cout << fmt::format ("Total time (activated > cemented):     {:>8.2f} ms/block avg\n", total_confirmation_time / (confirmed_count * 1000.0));
}
}