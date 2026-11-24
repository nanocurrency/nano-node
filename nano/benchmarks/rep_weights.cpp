/*
 * Benchmarks of the representative weight cache as the number of cached representatives grows: the lookup a vote's context takes, the batch a ballot's tally takes, the update a processed block makes and the snapshot the rep tiers take.
 * Every benchmark reports the time and the heap allocations per operation, so a change to the cache's table can be judged on both.
 *
 * The cache under test is a standalone `nano::rep_weights` bound to a dev node's store view and filled with `reps` representatives of random weight through `put`, which touches the cache only; the store is never read or written.
 *
 * Scenarios:
 * - get: the weight of a representative picked at random among the cached ones, as every vote's context takes it
 * - get_miss: the weight of an account without a cached weight
 * - tally: the weights of `tally_reps` representatives in one locked batch, as a ballot tallies its votes
 * - tally_map: the same batch returned as a map, as an election reads the weights of its voters
 * - put: a cached representative's weight set again under the unique lock, the cache's side of a processed block
 * - snapshot: a copy of the whole cache, as the rep tiers take on every refresh and the representatives RPCs on every call
 * - workload: one tally and one get per iteration with a put every `votes_per_block` iterations, about a node's mix of votes and blocks
 *
 * The reported time is the time one thread spends on one operation, the rate is the throughput of all threads together.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/node.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/test_common/system.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr size_t tally_reps = 100; // Principal representatives a ballot weighs
constexpr size_t tally_groups = 1024; // Distinct sets of representatives the tallies draw from
constexpr size_t votes_per_block = 100; // Votes a node counts per block it processes, so one cache update per that many tallies
constexpr size_t miss_accounts = 65536;

using account_list = std::vector<nano::account>;
using weight_list = std::vector<nano::uint128_t>;

nano::account random_account ()
{
	nano::account account;
	nano::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
	return account;
}

// A nonzero weight, so the cache keeps the representative
nano::uint128_t random_weight (std::mt19937_64 & rng)
{
	return nano::uint128_t{ rng () | 1 };
}

// The benchmarks' main leaves the network alone, a node must never start on another one from here
struct dev_network
{
	dev_network ()
	{
		nano::force_nano_dev_network ();
	}
};

class weight_fixture
{
public:
	// A cache with its representatives, built once per size
	struct cache
	{
		std::unique_ptr<nano::rep_weights> weights;
		account_list reps; // The cached representatives in random order
		account_list tallies; // `tally_groups` groups of `tally_reps` representatives picked at random
		weight_list updates; // Weights for the put benchmarks to cycle through
	};

	weight_fixture ()
	{
		node = system.add_node ();
		misses.resize (miss_accounts);
		std::ranges::generate (misses, random_account);
	}

	// The cache holding `count` representatives
	cache const & of (size_t count)
	{
		std::lock_guard guard{ mutex };
		auto & entry = caches[count];
		if (!entry.weights)
		{
			std::mt19937_64 rng{ count };
			entry.weights = std::make_unique<nano::rep_weights> (node->store.rep_weight);
			entry.reps.resize (count);
			std::ranges::generate (entry.reps, random_account);
			for (auto const & rep : entry.reps)
			{
				entry.weights->put (rep, random_weight (rng));
			}
			std::uniform_int_distribution<size_t> index{ 0, count - 1 };
			entry.tallies.resize (tally_groups * tally_reps);
			std::ranges::generate (entry.tallies, [&] { return entry.reps[index (rng)]; });
			entry.updates.resize (4096);
			std::ranges::generate (entry.updates, [&] { return random_weight (rng); });
		}
		return entry;
	}

	// One group of `tally_reps` representatives
	static std::span<nano::account const> tally (cache const & entry, size_t group)
	{
		return { entry.tallies.data () + (group % tally_groups) * tally_reps, tally_reps };
	}

public:
	dev_network network;
	nano::test::system system;
	std::shared_ptr<nano::node> node;
	account_list misses; // Accounts without a cached weight

private:
	std::mutex mutex; // Benchmark threads ask for the cache at the same time, it is built once under this lock
	std::map<size_t, cache> caches;
};

// Built on first use
weight_fixture & shared_fixture ()
{
	static weight_fixture instance;
	return instance;
}

// Every thread walks its own part of a list, so threads never share a representative
template <typename List>
auto const & pick (List const & list, benchmark::State const & state, size_t index)
{
	auto const part = list.size () / state.threads ();
	return list[state.thread_index () * part + index % part];
}

void rep_counts (benchmark::internal::Benchmark * benchmark)
{
	benchmark->ArgName ("reps")->Arg (64)->Arg (1'000)->Arg (10'000)->Arg (100'000)->UseRealTime ();
}
}

// The weight of a cached representative
static void BM_rep_weight_get (benchmark::State & state)
{
	auto const & entry = shared_fixture ().of (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "lookup" };
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (entry.weights->get (pick (entry.reps, state, index++)));
	}
	allocations.report (state);
}

// The weight of an account the cache does not hold
static void BM_rep_weight_get_miss (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const & entry = fixture.of (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "lookup" };
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (entry.weights->get (pick (fixture.misses, state, index++)));
	}
	allocations.report (state);
}

// The weights of `tally_reps` representatives written into a span, as a ballot tallies
static void BM_rep_weight_tally (benchmark::State & state)
{
	auto const & entry = shared_fixture ().of (state.range (0));
	weight_list weights (tally_reps);
	nano::benchmarks::allocation_counter allocations{ "tally" };
	size_t index = state.thread_index () * tally_groups / state.threads ();
	for (auto _ : state)
	{
		entry.weights->get (weight_fixture::tally (entry, index++), weights);
		benchmark::DoNotOptimize (weights.data ());
	}
	allocations.report (state);
}

// The weights of `tally_reps` representatives returned as a map, as an election reads its voters
static void BM_rep_weight_tally_map (benchmark::State & state)
{
	auto const & entry = shared_fixture ().of (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "tally" };
	size_t index = state.thread_index () * tally_groups / state.threads ();
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (entry.weights->get (weight_fixture::tally (entry, index++)));
	}
	allocations.report (state);
}

// A cached representative's weight set again
static void BM_rep_weight_put (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const & entry = fixture.of (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "put" };
	size_t index = 0;
	for (auto _ : state)
	{
		entry.weights->put (pick (entry.reps, state, index), entry.updates[index % entry.updates.size ()]);
		++index;
	}
	allocations.report (state);
}

// A copy of the whole cache
static void BM_rep_weight_snapshot (benchmark::State & state)
{
	auto const & entry = shared_fixture ().of (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "snapshot" };
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (entry.weights->get_all ());
	}
	allocations.report (state);
}

// A tally and a lookup per vote, with an update every `votes_per_block` votes
static void BM_rep_weight_workload (benchmark::State & state)
{
	auto const & entry = shared_fixture ().of (state.range (0));
	weight_list weights (tally_reps);
	nano::benchmarks::allocation_counter allocations{ "vote" };
	size_t index = 0;
	size_t group = state.thread_index () * tally_groups / state.threads ();
	for (auto _ : state)
	{
		entry.weights->get (weight_fixture::tally (entry, group++), weights);
		benchmark::DoNotOptimize (weights.data ());
		benchmark::DoNotOptimize (entry.weights->get (pick (entry.reps, state, index)));
		if (index % votes_per_block == 0)
		{
			entry.weights->put (pick (entry.reps, state, index / votes_per_block), entry.updates[index % entry.updates.size ()]);
		}
		++index;
	}
	allocations.report (state);
}

// clang-format off
BENCHMARK (BM_rep_weight_get)->Apply (rep_counts)->Threads (1)->Threads (4);
BENCHMARK (BM_rep_weight_get_miss)->Apply (rep_counts);
BENCHMARK (BM_rep_weight_tally)->Apply (rep_counts)->Threads (1)->Threads (4);
BENCHMARK (BM_rep_weight_tally_map)->Apply (rep_counts);
BENCHMARK (BM_rep_weight_put)->Apply (rep_counts);
BENCHMARK (BM_rep_weight_snapshot)->Apply (rep_counts);
BENCHMARK (BM_rep_weight_workload)->Apply (rep_counts)->Threads (1)->Threads (4);
// clang-format on
