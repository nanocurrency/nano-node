/*
 * Benchmarks of the duplicate filter on the path every inbound publish and vote takes: hashing the payload and inserting its digest into the table.
 * The filter ages its entries in epochs the network advances, so an insert never reads the clock; the clock scenarios add the steady clock read that time-stamped entries would need on every insert, to show what that choice saves.
 *
 * The filter has the node's default size and is shared by all benchmarks and threads, so it stays full as on a busy node.
 * Payloads are `payload_count` distinct random byte strings of the benchmark's size argument (136 bytes is a single-hash vote, 216 a state block publish), each thread cycling through them from its own offset.
 *
 * Scenarios:
 * - insert: hash the payload and insert its digest, as deserialization does
 * - insert_clock: the same after a steady clock read
 * - insert_digest: insert a precomputed digest, the table side alone
 * - insert_digest_clock: the same after a steady clock read
 * - clock: a steady clock read alone
 *
 * The reported time is the time one thread spends on one operation, the rate is the throughput of all threads together.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/lib/duplicate_filter.hpp>
#include <nano/node/network.hpp>
#include <nano/secure/network_params.hpp>

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr std::size_t payload_count = 65536;

nano::duplicate_filter & shared_filter ()
{
	static nano::duplicate_filter filter{ nano::network_config{ nano::dev::network_params.network }.duplicate_filter_size };
	return filter;
}

std::vector<std::vector<uint8_t>> make_payloads (std::size_t size)
{
	std::mt19937_64 rng{ size };
	std::vector<std::vector<uint8_t>> payloads (payload_count);
	for (auto & payload : payloads)
	{
		payload.resize (size);
		for (auto & byte : payload)
		{
			byte = static_cast<uint8_t> (rng ());
		}
	}
	return payloads;
}

std::vector<nano::duplicate_filter::digest> make_digests (std::size_t size)
{
	std::vector<nano::duplicate_filter::digest> digests;
	digests.reserve (payload_count);
	for (auto const & payload : make_payloads (size))
	{
		digests.push_back (shared_filter ().hash (payload));
	}
	return digests;
}

// Where a thread starts in the payloads, so threads do not insert the same one at the same time
std::size_t start_index (benchmark::State const & state)
{
	return static_cast<std::size_t> (state.thread_index ()) * (payload_count / static_cast<std::size_t> (state.threads ()));
}
}

static void BM_duplicate_filter_insert (benchmark::State & state)
{
	auto & filter = shared_filter ();
	auto const payloads = make_payloads (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (payloads[index]));
		index = (index + 1) % payload_count;
	}
}

BENCHMARK (BM_duplicate_filter_insert)->Arg (136)->Arg (216)->Threads (1)->Threads (8);

static void BM_duplicate_filter_insert_clock (benchmark::State & state)
{
	auto & filter = shared_filter ();
	auto const payloads = make_payloads (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		auto const now = std::chrono::steady_clock::now ();
		benchmark::DoNotOptimize (now);
		benchmark::DoNotOptimize (filter.insert (payloads[index]));
		index = (index + 1) % payload_count;
	}
}

BENCHMARK (BM_duplicate_filter_insert_clock)->Arg (136)->Arg (216)->Threads (1)->Threads (8);

static void BM_duplicate_filter_insert_digest (benchmark::State & state)
{
	auto & filter = shared_filter ();
	auto const digests = make_digests (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (digests[index]));
		index = (index + 1) % payload_count;
	}
}

BENCHMARK (BM_duplicate_filter_insert_digest)->Arg (216)->Threads (1)->Threads (8);

static void BM_duplicate_filter_insert_digest_clock (benchmark::State & state)
{
	auto & filter = shared_filter ();
	auto const digests = make_digests (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		auto const now = std::chrono::steady_clock::now ();
		benchmark::DoNotOptimize (now);
		benchmark::DoNotOptimize (filter.insert (digests[index]));
		index = (index + 1) % payload_count;
	}
}

BENCHMARK (BM_duplicate_filter_insert_digest_clock)->Arg (216)->Threads (1)->Threads (8);

static void BM_duplicate_filter_clock (benchmark::State & state)
{
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (std::chrono::steady_clock::now ());
	}
}

BENCHMARK (BM_duplicate_filter_clock)->Threads (1)->Threads (8);
