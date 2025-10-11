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
#include <nano/secure/ledger.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include <fmt/format.h>

namespace nano::cli
{
struct node_info
{
	std::shared_ptr<nano::node> node;
	nano::keypair representative_key; // Only used if this is a representative node
	bool is_representative;
	size_t index;
};

class pipeline_multinode_benchmark : public benchmark_base
{
private:
	std::vector<node_info> nodes;
	std::vector<std::unique_ptr<nano::thread_runner>> runners;

	struct block_timing
	{
		std::chrono::steady_clock::time_point submitted;
		std::chrono::steady_clock::time_point first_processed;
		std::chrono::steady_clock::time_point first_election_started;
		std::chrono::steady_clock::time_point first_election_confirmed;
		std::chrono::steady_clock::time_point first_cemented;
	};

	// Track timing for each block through the pipeline
	nano::locked<std::unordered_map<nano::block_hash, block_timing>> block_timings;

	// Track blocks waiting to be confirmed and cemented across all nodes
	nano::locked<std::unordered_map<nano::block_hash, std::chrono::steady_clock::time_point>> pending_cementing;

	// Metrics - track first occurrence across all nodes
	std::atomic<size_t> blocks_processed{ 0 };
	std::atomic<size_t> elections_started{ 0 };
	std::atomic<size_t> elections_confirmed{ 0 };
	std::atomic<size_t> blocks_cemented{ 0 };

	// Per-node metrics (protected by mutex since std::atomic can't be in vector)
	nano::locked<std::vector<size_t>> blocks_processed_per_node;
	nano::locked<std::vector<size_t>> elections_started_per_node;
	nano::locked<std::vector<size_t>> blocks_cemented_per_node;

public:
	pipeline_multinode_benchmark (benchmark_config const & config_a);

	void setup_nodes ();
	void setup_peering ();
	void wait_for_peering ();
	void setup_observers (size_t node_idx);
	void distribute_voting_weight ();
	void seed_transaction_pool ();
	void run ();
	void run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks);
	void print_statistics ();
	void cleanup ();
};

void run_pipeline_multinode_benchmark (boost::program_options::variables_map const & vm, std::filesystem::path const & data_path)
{
	auto config = benchmark_config::parse (vm);

	std::cout << "=== BENCHMARK: Multi-Node Pipeline ===\n";
	std::cout << "Configuration:\n";
	std::cout << fmt::format ("  Representative nodes: {}\n", config.num_representatives);
	std::cout << fmt::format ("  Observer nodes: {}\n", config.num_observers);
	std::cout << fmt::format ("  Total nodes: {}\n", config.num_representatives + config.num_observers);
	std::cout << fmt::format ("  Accounts: {}\n", config.num_accounts);
	std::cout << fmt::format ("  Iterations: {}\n", config.num_iterations);
	std::cout << fmt::format ("  Batch size: {}\n", config.batch_size);

	// Setup
	nano::network_constants::set_active_network ("dev");
	nano::logger::initialize (nano::log_config::cli_default (nano::log::level::warn));

	// Run benchmark
	pipeline_multinode_benchmark benchmark{ config };
	benchmark.run ();
	benchmark.cleanup ();
}

pipeline_multinode_benchmark::pipeline_multinode_benchmark (benchmark_config const & config_a) :
	benchmark_base (nullptr, config_a) // node is set up separately for multi-node
{
	auto const total_nodes = config.num_representatives + config.num_observers;
	blocks_processed_per_node.lock ()->resize (total_nodes, 0);
	elections_started_per_node.lock ()->resize (total_nodes, 0);
	blocks_cemented_per_node.lock ()->resize (total_nodes, 0);
}

void pipeline_multinode_benchmark::setup_nodes ()
{
	std::cout << "\nSetting up nodes...\n";

	nano::node_flags node_flags;
	// node_flags.disable_lazy_bootstrap = true;
	// node_flags.disable_legacy_bootstrap = true;
	// node_flags.disable_wallet_bootstrap = true;

	nano::work_pool work_pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	auto const total_nodes = config.num_representatives + config.num_observers;

	for (size_t i = 0; i < total_nodes; ++i)
	{
		bool is_representative = i < config.num_representatives;

		// Load configuration
		auto daemon_config = nano::load_config_file<nano::daemon_config> (nano::node_config_filename, {}, node_flags.config_overrides);
		auto node_config = daemon_config.node;

		// Common configuration
		node_config.network_params.work = nano::work_thresholds{ 0, 0, 0 };
		node_config.peering_port = 0; // Use random available port
		node_config.max_backlog = 0; // Disable bounded backlog
		node_config.block_processor.max_peer_queue = std::numeric_limits<size_t>::max ();
		node_config.block_processor.max_system_queue = std::numeric_limits<size_t>::max ();
		node_config.max_unchecked_blocks = 1024 * 1024;
		node_config.vote_processor.max_pr_queue = std::numeric_limits<size_t>::max ();
		node_config.priority_bucket.max_blocks = std::numeric_limits<size_t>::max ();
		node_config.priority_bucket.max_elections = std::numeric_limits<size_t>::max ();
		node_config.priority_bucket.reserved_elections = std::numeric_limits<size_t>::max ();

		// Enable vote generation for representatives
		if (is_representative)
		{
			node_config.enable_voting = true;
		}

		// Create dummy io_context (not used by node anymore, each node has its own)
		auto dummy_io_ctx = std::make_shared<boost::asio::io_context> ();

		// Create node
		auto node = std::make_shared<nano::node> (dummy_io_ctx, nano::unique_path (), node_config, work_pool, node_flags);

		node_info info;
		info.node = node;
		info.is_representative = is_representative;
		info.index = i;

		if (is_representative)
		{
			// Generate unique representative key
			info.representative_key = nano::keypair{};

			// Insert representative key into wallet for voting
			auto wallet = node->wallets.create (nano::random_wallet_id ());
			wallet->insert_adhoc (info.representative_key.prv);
		}

		nodes.push_back (info);

		node->start ();

		std::cout << fmt::format ("  Node {} ({}): port {}\n",
		i,
		is_representative ? "representative" : "observer",
		node->network.endpoint ().port ());
	}

	std::cout << "\n";
}

void pipeline_multinode_benchmark::setup_peering ()
{
	std::cout << "Setting up peering (fully connected mesh)...\n";

	// Connect all nodes to each other (full mesh topology)
	for (size_t i = 0; i < nodes.size (); ++i)
	{
		for (size_t j = i + 1; j < nodes.size (); ++j)
		{
			auto & node_i = nodes[i].node;
			auto & node_j = nodes[j].node;

			// Connect node i to node j
			node_i->network.merge_peer (node_j->network.endpoint ());
		}
	}
}

void pipeline_multinode_benchmark::wait_for_peering ()
{
	std::cout << "Waiting for peering to establish...\n";

	auto const expected_peers = nodes.size () - 1;
	auto const start_time = std::chrono::steady_clock::now ();
	auto const timeout = std::chrono::seconds (30);

	bool all_connected = false;
	while (!all_connected && (std::chrono::steady_clock::now () - start_time) < timeout)
	{
		all_connected = true;
		for (size_t i = 0; i < nodes.size (); ++i)
		{
			auto peer_count = nodes[i].node->network.size ();
			if (peer_count < expected_peers)
			{
				all_connected = false;
				break;
			}
		}

		if (!all_connected)
		{
			std::this_thread::sleep_for (100ms);
		}
	}

	if (all_connected)
	{
		std::cout << fmt::format ("All nodes connected ({} peers each)\n", expected_peers);
	}
	else
	{
		std::cout << "Warning: Not all nodes fully connected\n";
		for (size_t i = 0; i < nodes.size (); ++i)
		{
			std::cout << fmt::format ("  Node {}: {} peers\n", i, nodes[i].node->network.size ());
		}
	}

	std::cout << "\n";
}

void pipeline_multinode_benchmark::setup_observers (size_t node_idx)
{
	auto & info = nodes[node_idx];

	// Track when blocks get processed (first time across all nodes)
	info.node->ledger_notifications.blocks_processed.add ([this, node_idx] (std::deque<std::pair<nano::block_status, nano::block_context>> const & batch) {
		auto now = std::chrono::steady_clock::now ();
		auto timings_l = block_timings.lock ();

		for (auto const & [status, context] : batch)
		{
			if (status == nano::block_status::progress)
			{
				auto per_node_l = blocks_processed_per_node.lock ();
				(*per_node_l)[node_idx]++;

				if (auto it = timings_l->find (context.block->hash ()); it != timings_l->end ())
				{
					// Record first processing time
					if (it->second.first_processed == std::chrono::steady_clock::time_point{})
					{
						it->second.first_processed = now;
						blocks_processed++;
					}
				}
			}
		}
	});

	// Track when elections start
	info.node->active.election_started.add ([this, node_idx] (std::shared_ptr<nano::election> const & election, nano::bucket_index const & bucket, nano::priority_timestamp const & priority) {
		auto now = std::chrono::steady_clock::now ();
		auto hash = election->winner ()->hash ();
		auto timings_l = block_timings.lock ();

		auto per_node_l = elections_started_per_node.lock ();
		(*per_node_l)[node_idx]++;

		if (auto it = timings_l->find (hash); it != timings_l->end ())
		{
			// Record first election start time
			if (it->second.first_election_started == std::chrono::steady_clock::time_point{})
			{
				it->second.first_election_started = now;
				elections_started++;
			}
		}
	});

	// Track when elections get confirmed
	info.node->active.election_erased.add ([this, node_idx] (std::shared_ptr<nano::election> const & election) {
		if (!election->confirmed ())
		{
			return;
		}

		auto now = std::chrono::steady_clock::now ();
		auto hash = election->winner ()->hash ();
		auto timings_l = block_timings.lock ();

		if (auto it = timings_l->find (hash); it != timings_l->end ())
		{
			// Record first confirmation time
			if (it->second.first_election_confirmed == std::chrono::steady_clock::time_point{})
			{
				it->second.first_election_confirmed = now;
				elections_confirmed++;
			}
		}
	});

	// Track when blocks get cemented
	info.node->cementing_set.batch_cemented.add ([this, node_idx] (auto const & hashes) {
		auto now = std::chrono::steady_clock::now ();
		auto pending_l = pending_cementing.lock ();
		auto timings_l = block_timings.lock ();

		for (auto const & ctx : hashes)
		{
			auto hash = ctx.block->hash ();

			auto per_node_l = blocks_cemented_per_node.lock ();
			(*per_node_l)[node_idx]++;

			if (auto it = timings_l->find (hash); it != timings_l->end ())
			{
				// Record first cementing time
				if (it->second.first_cemented == std::chrono::steady_clock::time_point{})
				{
					it->second.first_cemented = now;
					blocks_cemented++;
					pending_l->erase (hash);
				}
			}
		}
	});
}

void pipeline_multinode_benchmark::distribute_voting_weight ()
{
	std::cout << "Distributing voting weight to representatives...\n";

	// We'll distribute the genesis balance equally among all representatives
	auto const total_balance = nano::dev::constants.genesis_amount;
	auto const balance_per_rep = total_balance / config.num_representatives;

	// Use first node to process the distribution blocks
	auto & primary_node = nodes[0].node;

	std::cout << fmt::format ("  Balance per representative: {}\n", balance_per_rep);

	// Create send/open pairs for each representative
	auto genesis_account = nano::dev::genesis_key.pub;
	auto genesis_balance = total_balance;
	auto genesis_frontier = nano::dev::genesis->hash ();

	for (size_t i = 0; i < config.num_representatives; ++i)
	{
		auto & rep_key = nodes[i].representative_key;

		// Create send block from genesis
		nano::block_builder builder;
		auto send = builder.state ()
						.account (genesis_account)
						.previous (genesis_frontier)
						.representative (genesis_account)
						.balance (genesis_balance - balance_per_rep)
						.link (rep_key.pub)
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (0)
						.build ();

		genesis_balance -= balance_per_rep;
		genesis_frontier = send->hash ();

		// Create open block for representative
		auto open = builder.state ()
						.account (rep_key.pub)
						.previous (0)
						.representative (rep_key.pub) // Self-represent
						.balance (balance_per_rep)
						.link (send->hash ())
						.sign (rep_key.prv, rep_key.pub)
						.work (0)
						.build ();

		// Process blocks on primary node
		{
			auto transaction = primary_node->ledger.tx_begin_write ();
			auto result = primary_node->ledger.process (transaction, send);
			release_assert (result == nano::block_status::progress, to_string (result));

			result = primary_node->ledger.process (transaction, open);
			release_assert (result == nano::block_status::progress, to_string (result));

			// Confirm both blocks
			auto cemented = primary_node->ledger.confirm (transaction, send->hash ());
			release_assert (!cemented.empty ());
			cemented = primary_node->ledger.confirm (transaction, open->hash ());
			release_assert (!cemented.empty ());
		}

		std::cout << fmt::format ("  Representative {}: {} (weight: {})\n",
		i, rep_key.pub.to_account (), balance_per_rep);
	}

	// Wait for distribution to propagate to all nodes
	std::cout << "Waiting for weight distribution to propagate...\n";
	std::this_thread::sleep_for (2s);

	std::cout << "\n";
}

void pipeline_multinode_benchmark::seed_transaction_pool ()
{
	std::cout << "Seeding transaction pool from first representative...\n";

	// Transfer 1% of first representative's balance to seed transactions
	auto & first_rep = nodes[0];
	auto const total_balance = nano::dev::constants.genesis_amount / config.num_representatives;
	auto const seed_amount = total_balance / 100; // 1% for transactions
	auto const remaining_balance = total_balance - seed_amount;

	// Get representative's current frontier
	auto rep_frontier = node->latest (first_rep.representative_key.pub);

	// Select random account to receive seed funds
	auto target_account = pool.get_random_account ();
	auto & target_keypair = pool.get_keypair (target_account);

	// Create send block from representative to target account
	nano::block_builder builder;
	auto send = builder.state ()
					.account (first_rep.representative_key.pub)
					.previous (rep_frontier)
					.representative (first_rep.representative_key.pub) // Keep self-representing
					.balance (remaining_balance)
					.link (target_account)
					.sign (first_rep.representative_key.prv, first_rep.representative_key.pub)
					.work (0)
					.build ();

	// Create open block for target account
	auto open = builder.state ()
					.account (target_account)
					.previous (0)
					.representative (target_account)
					.balance (seed_amount)
					.link (send->hash ())
					.sign (target_keypair.prv, target_keypair.pub)
					.work (0)
					.build ();

	// Process blocks on primary node
	{
		auto transaction = node->ledger.tx_begin_write ();
		auto result = node->ledger.process (transaction, send);
		release_assert (result == nano::block_status::progress, to_string (result));

		result = node->ledger.process (transaction, open);
		release_assert (result == nano::block_status::progress, to_string (result));

		// Confirm both blocks
		auto cemented = node->ledger.confirm (transaction, send->hash ());
		release_assert (!cemented.empty ());
		cemented = node->ledger.confirm (transaction, open->hash ());
		release_assert (!cemented.empty ());
	}

	// Update pool balance tracking
	pool.set_initial_balance (target_account, seed_amount);
	pool.set_frontier (target_account, open->hash ());

	std::cout << fmt::format ("Transaction pool seeded: {} transferred to {}\n",
	seed_amount, target_account.to_account ());
	std::cout << fmt::format ("Representative 0 remaining balance: {} (retaining voting weight)\n\n", remaining_balance);

	// Wait for seed to propagate
	std::this_thread::sleep_for (500ms);
}

void pipeline_multinode_benchmark::run ()
{
	setup_nodes ();

	// Setup observers for all nodes
	for (size_t i = 0; i < nodes.size (); ++i)
	{
		setup_observers (i);
	}

	setup_peering ();
	wait_for_peering ();

	// Distribute voting weight to representatives
	distribute_voting_weight ();

	// Use first node for account pool and block generation
	node = nodes[0].node;

	std::cout << fmt::format ("Generating {} accounts...\n", config.num_accounts);
	pool.generate_accounts (config.num_accounts);

	// Transfer small amount from first representative to seed transactions
	// (Genesis balance was already distributed to representatives)
	seed_transaction_pool ();

	std::cout << "\nSystem Info:\n";
	std::cout << fmt::format ("  Backend: {}\n", node->store.vendor_get ());
	std::cout << fmt::format ("  Representative nodes: {}\n", config.num_representatives);
	std::cout << fmt::format ("  Observer nodes: {}\n", config.num_observers);
	std::cout << fmt::format ("  Total nodes: {}\n", nodes.size ());
	std::cout << "\n";

	for (size_t iteration = 0; iteration < config.num_iterations; ++iteration)
	{
		std::cout << fmt::format ("\n--- Iteration {}/{} --------------------------------------------------------------\n", iteration + 1, config.num_iterations);
		std::cout << fmt::format ("Generating {} random transfers...\n", config.batch_size / 2);
		auto blocks = generate_random_transfers ();

		std::cout << fmt::format ("Measuring multi-node pipeline for {} blocks...\n", blocks.size ());
		run_iteration (blocks);
	}

	print_statistics ();
}

void pipeline_multinode_benchmark::run_iteration (std::deque<std::shared_ptr<nano::block>> & blocks)
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

	// Submit all blocks to the first node only
	// They should propagate to other nodes through the network
	std::cout << fmt::format ("Submitting {} blocks to node 0...\n", blocks.size ());
	auto & primary_node = nodes[0].node;
	while (!blocks.empty ())
	{
		auto block = blocks.front ();
		blocks.pop_front ();

		// Process block through full confirmation pipeline
		primary_node->process_active (block);
	}

	// Wait for all blocks to be confirmed and cemented on at least one node
	nano::interval progress_interval;
	while (true)
	{
		{
			auto pending_l = pending_cementing.lock ();
			if (pending_l->empty () || progress_interval.elapse (3s))
			{
				// Show aggregate stats across all nodes
				size_t total_block_processor_size = 0;
				size_t total_active_size = 0;
				size_t total_cementing_size = 0;

				for (auto const & node_info : nodes)
				{
					total_block_processor_size += node_info.node->block_processor.size ();
					total_active_size += node_info.node->active.size ();
					total_cementing_size += node_info.node->cementing_set.size ();
				}

				std::cout << fmt::format ("Blocks remaining: {:>9} (agg: block_proc {:>9} | active {:>5} | cementing {:>5})\n",
				pending_l->size (),
				total_block_processor_size,
				total_active_size,
				total_cementing_size);
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

	// Clear stats on all nodes
	for (auto & node_info : nodes)
	{
		node_info.node->stats.clear ();
	}
}

void pipeline_multinode_benchmark::print_statistics ()
{
	std::cout << "\n--- SUMMARY ---------------------------------------------------------------------\n\n";
	std::cout << "Network Topology:\n";
	std::cout << fmt::format ("  Representative nodes:    {:>10}\n", config.num_representatives);
	std::cout << fmt::format ("  Observer nodes:          {:>10}\n", config.num_observers);
	std::cout << fmt::format ("  Total nodes:             {:>10}\n", nodes.size ());
	std::cout << "\n";

	std::cout << "Aggregate Performance (first occurrence across all nodes):\n";
	std::cout << fmt::format ("  Blocks processed:        {:>10}\n", blocks_processed.load ());
	std::cout << fmt::format ("  Elections started:       {:>10}\n", elections_started.load ());
	std::cout << fmt::format ("  Elections confirmed:     {:>10}\n", elections_confirmed.load ());
	std::cout << fmt::format ("  Blocks cemented:         {:>10}\n", blocks_cemented.load ());
	std::cout << "\n";

	std::cout << "Per-Node Statistics:\n";
	{
		auto processed_l = blocks_processed_per_node.lock ();
		auto elections_l = elections_started_per_node.lock ();
		auto cemented_l = blocks_cemented_per_node.lock ();

		for (size_t i = 0; i < nodes.size (); ++i)
		{
			std::cout << fmt::format ("  Node {} ({:>14}): processed {:>8} | elections {:>8} | cemented {:>8} | peers {:>2}\n",
			i,
			nodes[i].is_representative ? "representative" : "observer",
			(*processed_l)[i],
			(*elections_l)[i],
			(*cemented_l)[i],
			nodes[i].node->network.size ());
		}
	}
	std::cout << "\n";

	std::cout << fmt::format ("Accounts total:          {:>10}\n", pool.total_accounts ());
	std::cout << fmt::format ("Accounts with balance:   {:>10} ({:.1f}%)\n",
	pool.accounts_with_balance_count (),
	100.0 * pool.accounts_with_balance_count () / pool.total_accounts ());

	// Calculate timing statistics from raw data
	auto timings_l = block_timings.lock ();

	uint64_t total_processing_time = 0;
	uint64_t total_activation_time = 0;
	uint64_t total_confirmation_time = 0;
	uint64_t total_pipeline_time = 0;
	size_t processed_count = 0;
	size_t activation_count = 0;
	size_t confirmation_count = 0;
	size_t cemented_count = 0;

	for (auto const & [hash, timing] : *timings_l)
	{
		if (timing.first_processed != std::chrono::steady_clock::time_point{})
		{
			total_processing_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.first_processed - timing.submitted).count ();
			processed_count++;
		}

		if (timing.first_election_started != std::chrono::steady_clock::time_point{})
		{
			total_activation_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.first_election_started - timing.first_processed).count ();
			activation_count++;
		}

		if (timing.first_election_confirmed != std::chrono::steady_clock::time_point{})
		{
			total_confirmation_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.first_election_confirmed - timing.first_election_started).count ();
			confirmation_count++;
		}

		if (timing.first_cemented != std::chrono::steady_clock::time_point{})
		{
			total_pipeline_time += std::chrono::duration_cast<std::chrono::microseconds> (timing.first_cemented - timing.submitted).count ();
			cemented_count++;
		}
	}

	std::cout << "\n";
	std::cout << "Pipeline Timing (time to first occurrence across all nodes):\n";
	if (processed_count > 0)
	{
		std::cout << fmt::format ("  Block processing (submitted > processed):    {:>8.2f} ms/block avg\n", total_processing_time / (processed_count * 1000.0));
	}
	if (activation_count > 0)
	{
		std::cout << fmt::format ("  Election activation (processed > activated): {:>8.2f} ms/block avg\n", total_activation_time / (activation_count * 1000.0));
	}
	if (confirmation_count > 0)
	{
		std::cout << fmt::format ("  Election confirmation (activated > confirmed): {:>8.2f} ms/block avg\n", total_confirmation_time / (confirmation_count * 1000.0));
	}
	if (cemented_count > 0)
	{
		std::cout << fmt::format ("  Total pipeline (submitted > cemented):       {:>8.2f} ms/block avg\n", total_pipeline_time / (cemented_count * 1000.0));
	}
}

void pipeline_multinode_benchmark::cleanup ()
{
	std::cout << "\nStopping nodes...\n";
	for (auto & node_info : nodes)
	{
		node_info.node->stop ();
	}
}
}
