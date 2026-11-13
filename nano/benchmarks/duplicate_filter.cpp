/*
 * Benchmarks of duplicate filtering on the path every inbound publish and vote takes: hashing the payload and inserting its digest into the table.
 * Two classes are measured. `nano::duplicate_filter` is the table itself, which holds no lock, so its scenarios run on one thread. `nano::transport::message_filter` is the front for it that the network shares between its threads, adding the lock and the domain of the message, so its scenarios run on 1 to 16 threads to show how it behaves as more of them contend.
 *
 * Each filter has the node's default size and is shared by its benchmarks and their threads, so it stays full as on a busy node.
 * Payloads are `payload_count` distinct random byte strings of the benchmark's size argument (136 bytes is a single-hash vote, 216 a state block publish), each thread cycling through them from its own offset.
 *
 * Scenarios:
 * - duplicate_filter_insert: hash the payload and insert its digest into the plain filter
 * - duplicate_filter_insert_digest: insert a precomputed digest into the plain filter, the table side alone
 * - message_filter_insert: hash the payload and insert its digest, as the TCP server does for a message it reads
 * - message_filter_insert_digest: insert a precomputed digest, the lock and the table side alone
 *
 * The reported time is the time one thread spends on one operation, the rate is the throughput of all threads together.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/lib/block_type.hpp>
#include <nano/lib/duplicate_filter.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/messages/message_type.hpp>
#include <nano/node/network.hpp>
#include <nano/node/transport/message_filter.hpp>
#include <nano/secure/network_params.hpp>

#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr std::size_t payload_count = 65536;

// Domain the plain filter's inputs go in under, any one value does
constexpr uint64_t plain_domain = 1;

// The plain filter, which holds no lock, so only single-threaded benchmarks use it
nano::duplicate_filter & plain_filter ()
{
	static nano::duplicate_filter filter{ nano::network_config{ nano::dev::network_params.network }.duplicate_filter_size };
	return filter;
}

// The filter as the network holds it, shared by the threads of a benchmark
nano::transport::message_filter & shared_message_filter ()
{
	static nano::network_config const config{ nano::dev::network_params.network };
	static nano::transport::message_filter filter{ config };
	return filter;
}

// Header of the message a payload of this size stands for: a vote for the single-hash size, a state block publish otherwise
nano::messages::message_header make_header (std::size_t payload_size)
{
	auto const & network = nano::dev::network_params.network;
	if (payload_size == 136)
	{
		return nano::messages::message_header{ network, nano::messages::message_type::confirm_ack };
	}

	nano::messages::message_header header{ network, nano::messages::message_type::publish };
	header.block_type_set (nano::block_type::state);
	return header;
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

// Digests of the payloads as the plain filter hashes them
std::vector<nano::filter_digest> make_plain_digests (std::size_t size)
{
	std::vector<nano::filter_digest> digests;
	digests.reserve (payload_count);
	for (auto const & payload : make_payloads (size))
	{
		digests.push_back (plain_filter ().hash (payload, plain_domain));
	}
	return digests;
}

// Digests of the payloads as the message filter hashes them
std::vector<nano::filter_digest> make_message_digests (std::size_t size)
{
	auto const header = make_header (size);
	std::vector<nano::filter_digest> digests;
	digests.reserve (payload_count);
	for (auto const & payload : make_payloads (size))
	{
		digests.push_back (shared_message_filter ().hash (header, payload));
	}
	return digests;
}

// Where a thread starts in the payloads, so threads do not insert the same one at the same time
std::size_t start_index (benchmark::State const & state)
{
	return static_cast<std::size_t> (state.thread_index ()) * (payload_count / static_cast<std::size_t> (state.threads ()));
}

// Reports how many inserts all threads together make per second
void report_rate (benchmark::State & state)
{
	// Rates are divided by the summed up time of all threads, scaling by the thread count turns that into the total throughput
	// The threaded scenarios measure real time, as a thread waiting for the lock uses none of its own
	auto const iterations = static_cast<double> (state.iterations ());
	state.counters["inserts/s"] = benchmark::Counter (iterations * state.threads (), benchmark::Counter::kIsRate);
}
}

static void BM_duplicate_filter_insert (benchmark::State & state)
{
	auto & filter = plain_filter ();
	auto const payloads = make_payloads (static_cast<std::size_t> (state.range (0)));
	std::size_t index = 0;

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (payloads[index], plain_domain));
		index = (index + 1) % payload_count;
	}

	report_rate (state);
}

BENCHMARK (BM_duplicate_filter_insert)->Arg (136)->Arg (216);

static void BM_duplicate_filter_insert_digest (benchmark::State & state)
{
	auto & filter = plain_filter ();
	auto const digests = make_plain_digests (static_cast<std::size_t> (state.range (0)));
	std::size_t index = 0;

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (digests[index]));
		index = (index + 1) % payload_count;
	}

	report_rate (state);
}

BENCHMARK (BM_duplicate_filter_insert_digest)->Arg (216);

static void BM_message_filter_insert (benchmark::State & state)
{
	auto & filter = shared_message_filter ();
	auto const header = make_header (static_cast<std::size_t> (state.range (0)));
	auto const payloads = make_payloads (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (filter.hash (header, payloads[index])));
		index = (index + 1) % payload_count;
	}

	report_rate (state);
}

BENCHMARK (BM_message_filter_insert)->Arg (136)->Arg (216)->ThreadRange (1, 16)->UseRealTime ();

static void BM_message_filter_insert_digest (benchmark::State & state)
{
	auto & filter = shared_message_filter ();
	auto const digests = make_message_digests (static_cast<std::size_t> (state.range (0)));
	auto index = start_index (state);

	for (auto _ : state)
	{
		benchmark::DoNotOptimize (filter.insert (digests[index]));
		index = (index + 1) % payload_count;
	}

	report_rate (state);
}

BENCHMARK (BM_message_filter_insert_digest)->Arg (216)->ThreadRange (1, 16)->UseRealTime ();
