/*
 * Benchmarks of block processing throughput and of the ledger writer contention it causes.
 *
 * The fixture is a dev network node with `account_count` opened accounts and everything but the block processor kept away from the ledger.
 * Every iteration submits `round_count` rounds of transfers, in each of which every account sends one raw to the next account and receives one raw from the previous one,
 * through the block processor from the bootstrap source, and waits until every block is in the ledger.
 * Alongside, a competing writer takes and releases the ledger write lock at a fixed pace, standing in for confirmation processing, and records how long each acquisition waited.
 *
 * Reported: blocks/s through the processor, the competing writer's wait percentiles and how many times it got the lock during the run.
 * The argument is the number of extra threads the processor verifies signatures with ahead of the ledger write, 0 verifies on the processing thread alone.
 * Keep the node's data directory on a RAM disk (NANO_APP_PATH) so the store does not measure the disk.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bounded_backlog.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/write_queue.hpp>
#include <nano/test_common/system.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

using namespace std::chrono_literals;

namespace
{
constexpr size_t account_count = 2000;
constexpr size_t round_count = 5;
constexpr nano::uint128_t funding = 1000; // Raw per account, every round moves one raw out and one in
constexpr auto writer_interval = 1ms;

// The benchmarks' main leaves the network alone, a node must never start on another one from here
struct dev_network
{
	dev_network ()
	{
		nano::force_nano_dev_network ();
	}
};

struct wait_percentiles
{
	double p50_us{ 0 };
	double p99_us{ 0 };
	double max_us{ 0 };
};

wait_percentiles percentiles (std::vector<std::chrono::steady_clock::duration> waits)
{
	wait_percentiles result;
	if (waits.empty ())
	{
		return result;
	}
	std::sort (waits.begin (), waits.end ());
	auto const micros = [] (std::chrono::steady_clock::duration duration) {
		return std::chrono::duration<double, std::micro> (duration).count ();
	};
	result.p50_us = micros (waits[waits.size () / 2]);
	result.p99_us = micros (waits[waits.size () * 99 / 100]);
	result.max_us = micros (waits.back ());
	return result;
}

// Takes and releases the ledger writer at a fixed pace and records how long each acquisition had to wait, as confirmation processing would
class competing_writer
{
public:
	explicit competing_writer (nano::ledger & ledger) :
		ledger{ ledger },
		thread{ [this] { run (); } }
	{
	}

	~competing_writer ()
	{
		stop ();
	}

	// Stops the writer and hands back every wait it recorded
	std::vector<std::chrono::steady_clock::duration> stop ()
	{
		stopped = true;
		if (thread.joinable ())
		{
			thread.join ();
		}
		return waits;
	}

private:
	void run ()
	{
		while (!stopped)
		{
			auto const start = std::chrono::steady_clock::now ();
			{
				auto transaction = ledger.tx_begin_write (nano::store::writer::confirmation_height);
				waits.push_back (std::chrono::steady_clock::now () - start);
			}
			std::this_thread::sleep_for (writer_interval);
		}
	}

	nano::ledger & ledger;
	std::atomic<bool> stopped{ false };
	std::vector<std::chrono::steady_clock::duration> waits;
	std::thread thread;
};

class block_processing_fixture
{
public:
	explicit block_processing_fixture (unsigned verification_threads)
	{
		auto config = system.default_config ();
		config.block_processor->verification_threads = verification_threads;
		// Nothing but the block processor may touch the ledger during a run
		config.backlog_scan->enable = false;
		config.bounded_backlog->enable = false;
		config.priority_scheduler->enable = false;
		config.hinted_scheduler->enable = false;
		config.optimistic_scheduler->enable = false;
		config.block_processor->enable_throttling = false;
		config.block_processor->max_system_queue = std::numeric_limits<size_t>::max ();
		nano::node_flags flags;
		flags.disable_rep_crawler = true;
		flags.disable_request_loop = true;
		flags.disable_ongoing_bootstrap = true;
		flags.disable_activate_successors = true;
		node = system.add_node (config, flags);
		open_accounts ();
	}

	// One round of transfers: every account sends one raw to the next account, then every account receives the raw sent to it
	std::deque<std::shared_ptr<nano::block>> transfer_round ()
	{
		nano::block_builder builder;
		std::deque<std::shared_ptr<nano::block>> blocks;
		std::vector<nano::block_hash> sends (accounts.size ());
		for (size_t i = 0; i < accounts.size (); ++i)
		{
			auto & sender = accounts[i];
			auto const & destination = accounts[(i + 1) % accounts.size ()];
			sender.balance -= 1;
			auto send = builder
						.state ()
						.account (sender.key.pub)
						.previous (sender.head)
						.representative (sender.key.pub)
						.balance (sender.balance)
						.link (destination.key.pub)
						.sign (sender.key.prv, sender.key.pub)
						.work (*system.work.generate (sender.head))
						.build ();
			sender.head = send->hash ();
			sends[i] = send->hash ();
			blocks.push_back (send);
		}
		for (size_t i = 0; i < accounts.size (); ++i)
		{
			auto & receiver = accounts[i];
			receiver.balance += 1;
			auto receive = builder
						   .state ()
						   .account (receiver.key.pub)
						   .previous (receiver.head)
						   .representative (receiver.key.pub)
						   .balance (receiver.balance)
						   .link (sends[(i + accounts.size () - 1) % accounts.size ()])
						   .sign (receiver.key.prv, receiver.key.pub)
						   .work (*system.work.generate (receiver.head))
						   .build ();
			receiver.head = receive->hash ();
			blocks.push_back (receive);
		}
		return blocks;
	}

	// Submits the blocks from the bootstrap source and waits until the processor has handled every one of them, all of which must have entered the ledger
	void process (std::deque<std::shared_ptr<nano::block>> const & blocks)
	{
		auto const target = node->ledger.block_count () + blocks.size ();
		submitted += blocks.size ();
		auto const result = node->block_processor.add_many (blocks, nano::block_source::bootstrap);
		release_assert (result.added == blocks.size ());
		while (node->stats.count (nano::stat::type::block_processor_source, nano::stat::detail::bootstrap) < submitted)
		{
			std::this_thread::sleep_for (100us);
		}
		release_assert (node->ledger.block_count () == target, "blocks were rejected by the ledger");
	}

	nano::test::system system;
	std::shared_ptr<nano::node> node;

private:
	struct account_state
	{
		nano::keypair key;
		nano::block_hash head;
		nano::uint128_t balance;
	};

	// Funds and opens every account from genesis, through the same path the measured blocks take
	void open_accounts ()
	{
		nano::block_builder builder;
		std::deque<std::shared_ptr<nano::block>> blocks;
		auto genesis_head = nano::dev::genesis->hash ();
		auto genesis_balance = nano::dev::constants.genesis_amount;
		for (size_t i = 0; i < account_count; ++i)
		{
			account_state account;
			genesis_balance -= funding;
			auto send = builder
						.state ()
						.account (nano::dev::genesis_key.pub)
						.previous (genesis_head)
						.representative (nano::dev::genesis_key.pub)
						.balance (genesis_balance)
						.link (account.key.pub)
						.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						.work (*system.work.generate (genesis_head))
						.build ();
			genesis_head = send->hash ();
			auto open = builder
						.state ()
						.account (account.key.pub)
						.previous (0)
						.representative (account.key.pub)
						.balance (funding)
						.link (send->hash ())
						.sign (account.key.prv, account.key.pub)
						.work (*system.work.generate (account.key.pub))
						.build ();
			account.head = open->hash ();
			account.balance = funding;
			blocks.push_back (send);
			blocks.push_back (open);
			accounts.push_back (std::move (account));
		}
		process (blocks);
	}

	std::vector<account_state> accounts;
	size_t submitted{ 0 }; // Blocks handed to the processor so far, which its source stat counts once they are processed
};

void BM_block_processing (benchmark::State & state)
{
	dev_network network;
	block_processing_fixture fixture{ static_cast<unsigned> (state.range (0)) };

	for (auto _ : state)
	{
		std::deque<std::shared_ptr<nano::block>> blocks;
		for (size_t round = 0; round < round_count; ++round)
		{
			auto round_blocks = fixture.transfer_round ();
			blocks.insert (blocks.end (), round_blocks.begin (), round_blocks.end ());
		}

		competing_writer writer{ fixture.node->ledger };
		auto const start = std::chrono::steady_clock::now ();
		fixture.process (blocks);
		auto const elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ();
		auto const waits = writer.stop ();

		state.SetIterationTime (elapsed);
		state.counters["blocks"] = static_cast<double> (blocks.size ());
		state.counters["blocks_per_s"] = static_cast<double> (blocks.size ()) / elapsed;
		auto const wait = percentiles (waits);
		state.counters["writer_wait_p50_us"] = wait.p50_us;
		state.counters["writer_wait_p99_us"] = wait.p99_us;
		state.counters["writer_wait_max_us"] = wait.max_us;
		state.counters["writer_acquisitions"] = static_cast<double> (waits.size ());
	}
}
BENCHMARK (BM_block_processing)->UseManualTime ()->Iterations (1)->Unit (benchmark::kMillisecond)->ArgName ("verification_threads")->Arg (0)->Arg (1)->Arg (2)->Arg (4)->Arg (8);
}
