/*
 * Benchmarks of the work a vote causes on a vote processor thread: the signature check, routing to elections, the vote cache and the observers.
 * Every benchmark reports the time and the heap allocations per vote, so a change can be judged on both.
 *
 * The fixture is a dev network node with `election_count` live elections and `rep_count` weighted representatives.
 * The representatives stay below quorum together, so the elections never confirm and the first vote of every representative is counted.
 *
 * Scenarios, named after what happens to the hashes of a vote:
 * - unmatched: no election claims the hash, the vote goes to the vote cache. Four representatives vote on each hash, so one insert is followed by three updates
 * - counted: the hash has a live election that counts the vote. Representatives vote one after another, so an election holds 0 to `rep_count` - 1 votes when the next arrives
 * - replay: the hash has a live election that already holds this vote
 * - late: the hash was confirmed recently
 *
 * The reported time is the time one thread spends on one vote, votes/s is the throughput of all threads together.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/vote.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/election.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>

#include <map>
#include <mutex>
#include <thread>

#include <benchmark/benchmark.h>

namespace
{
constexpr size_t rep_count = 64;
constexpr size_t election_count = 1024;
constexpr size_t unmatched_hashes = 81920; // More than the vote cache holds, so its eviction is part of the measurement
constexpr size_t unmatched_voters = 4;
constexpr size_t late_hashes = 32768;

using vote_list = std::vector<std::shared_ptr<nano::vote>>;

nano::block_hash random_hash ()
{
	nano::block_hash hash;
	nano::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	return hash;
}

// The benchmarks' main leaves the network alone, a node must never start on another one from here
struct dev_network
{
	dev_network ()
	{
		nano::force_nano_dev_network ();
	}
};

class vote_fixture
{
public:
	vote_fixture ()
	{
		auto config = system.default_config ();
		// Elections are started by hand, anything else starting them would make runs differ
		config.hinted_scheduler->enable = false;
		config.optimistic_scheduler->enable = false;
		config.priority_scheduler->enable = false;
		config.backlog_scan->enable = false;
		node = system.add_node (config);
		channel = std::make_shared<nano::transport::inproc::channel> (*node, *node);

		for (size_t i = 0; i < rep_count; ++i)
		{
			reps.push_back (nano::test::setup_rep (system, *node, 100 * nano::Knano_ratio));
		}
		blocks = nano::test::setup_independent_blocks (system, *node, election_count);

		for (size_t i = 0; i < late_hashes; ++i)
		{
			late.push_back (random_hash ());
			node->active.recently_confirmed.put ({ random_hash (), random_hash () }, late.back (), nano::election_status{});
		}
	}

	// Gives every block a fresh election without votes and empties the vote cache
	void restart_elections ()
	{
		node->active.clear ();
		node->vote_cache.clear ();
		for (auto const & block : blocks)
		{
			auto const result = node->active.insert (block, nano::election_behavior::priority);
			release_assert (result.inserted);
		}
		// A starting election reads the vote cache on another thread, that must not overlap a measurement
		while (!node->vote_cache_processor.empty ())
		{
			std::this_thread::yield ();
		}
	}

	// One vote per representative and group of `hashes` elections, ordered by representative
	vote_list const & election_votes (size_t hashes)
	{
		std::lock_guard guard{ mutex };
		auto & votes = election_votes_m[hashes];
		if (votes.empty ())
		{
			for (auto const & rep : reps)
			{
				for (size_t first = 0; first + hashes <= blocks.size (); first += hashes)
				{
					std::vector<nano::block_hash> group;
					for (size_t i = first; i < first + hashes; ++i)
					{
						group.push_back (blocks[i]->hash ());
					}
					votes.push_back (std::make_shared<nano::vote> (rep.pub, rep.prv, 1, 0, group));
				}
			}
		}
		return votes;
	}

	// Votes on hashes without an election, `unmatched_voters` representatives in a row vote on the same group of `hashes`
	vote_list const & unmatched_votes (size_t hashes)
	{
		std::lock_guard guard{ mutex };
		return random_votes (unmatched_votes_m[hashes], hashes, unmatched_hashes / hashes, unmatched_voters, [] (size_t) { return random_hash (); });
	}

	// Votes on recently confirmed hashes
	vote_list const & late_votes (size_t hashes)
	{
		std::lock_guard guard{ mutex };
		return random_votes (late_votes_m[hashes], hashes, late.size () / hashes, 1, [this] (size_t index) { return late[index]; });
	}

public:
	dev_network network;
	nano::test::system system;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::transport::channel> channel;
	std::vector<nano::keypair> reps;
	nano::block_list_t blocks; // One unconfirmed block per election

private:
	vote_list const & random_votes (vote_list & votes, size_t hashes, size_t groups, size_t voters, std::function<nano::block_hash (size_t)> const & hash_source)
	{
		debug_assert (!mutex.try_lock ());
		if (votes.empty ())
		{
			for (size_t group = 0; group < groups; ++group)
			{
				std::vector<nano::block_hash> hashes_l;
				for (size_t i = 0; i < hashes; ++i)
				{
					hashes_l.push_back (hash_source (group * hashes + i));
				}
				for (size_t voter = 0; voter < voters; ++voter)
				{
					auto const & rep = reps[(group + voter) % reps.size ()];
					votes.push_back (std::make_shared<nano::vote> (rep.pub, rep.prv, 1, 0, hashes_l));
				}
			}
		}
		return votes;
	}

	std::mutex mutex; // Benchmark threads ask for their votes at the same time, the lists are built once under this lock
	std::vector<nano::block_hash> late;
	std::map<size_t, vote_list> election_votes_m;
	std::map<size_t, vote_list> unmatched_votes_m;
	std::map<size_t, vote_list> late_votes_m;
};

// Built on first use, building it takes seconds
vote_fixture & shared_fixture ()
{
	static vote_fixture instance;
	return instance;
}

// Measures the allocations of the calling thread from construction until report
class allocation_counter
{
public:
	void report (benchmark::State & state) const
	{
		auto const used = nano::benchmarks::thread_allocations () - start;
		auto const votes = static_cast<double> (state.iterations ());
		state.counters["allocs/vote"] = benchmark::Counter (used.count / votes, benchmark::Counter::kAvgThreads);
		state.counters["bytes/vote"] = benchmark::Counter (used.bytes / votes, benchmark::Counter::kAvgThreads);
		// Rates are divided by the summed up time of all threads, scaling by the thread count turns that into the total throughput
		state.counters["votes/s"] = benchmark::Counter (votes * state.threads (), benchmark::Counter::kIsRate);
	}

private:
	nano::benchmarks::allocation_stats const start{ nano::benchmarks::thread_allocations () };
};

// Every thread walks its own part of the votes, so threads never share a hash
std::shared_ptr<nano::vote> const & pick (vote_list const & votes, benchmark::State const & state, size_t index)
{
	auto const part = votes.size () / state.threads ();
	return votes[state.thread_index () * part + index % part];
}

enum class entry_point
{
	router, // nano::vote_router::vote
	processor, // nano::vote_processor::vote_blocking, which adds the signature check and the vote observers
};

void deliver (vote_fixture & fixture, entry_point entry, std::shared_ptr<nano::vote> const & vote)
{
	if (entry == entry_point::router)
	{
		benchmark::DoNotOptimize (fixture.node->vote_router.vote (vote));
	}
	else
	{
		benchmark::DoNotOptimize (fixture.node->vote_processor.vote_blocking (vote, fixture.channel));
	}
}
}

// The signature check every vote passes before it is routed
static void BM_vote_validate (benchmark::State & state)
{
	auto const & votes = shared_fixture ().unmatched_votes (state.range (0));
	allocation_counter allocations;
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (pick (votes, state, index++)->validate ());
	}
	allocations.report (state);
}

static void BM_vote_unmatched (benchmark::State & state, entry_point entry)
{
	auto & fixture = shared_fixture ();
	auto const & votes = fixture.unmatched_votes (state.range (0));
	if (state.thread_index () == 0)
	{
		fixture.node->vote_cache.clear ();
	}
	allocation_counter allocations;
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, entry, pick (votes, state, index++));
	}
	allocations.report (state);
}

// Runs exactly once through the votes, a second vote of a representative would not be counted
static void BM_vote_counted (benchmark::State & state, entry_point entry)
{
	auto & fixture = shared_fixture ();
	auto const & votes = fixture.election_votes (state.range (0));
	if (state.thread_index () == 0)
	{
		fixture.restart_elections ();
	}
	release_assert (state.max_iterations * state.threads () == votes.size ());
	allocation_counter allocations;
	// Representatives are dealt out to the threads in turn, so every thread sees elections fill up the same way
	auto const groups = votes.size () / rep_count;
	size_t index = 0;
	for (auto _ : state)
	{
		auto const rep = state.thread_index () + state.threads () * (index / groups);
		deliver (fixture, entry, votes[rep * groups + index % groups]);
		++index;
	}
	allocations.report (state);
}

static void BM_vote_replay (benchmark::State & state, entry_point entry)
{
	auto & fixture = shared_fixture ();
	auto const & all = fixture.election_votes (state.range (0));
	// The votes of the first representative, delivered once before the measurement
	vote_list const votes{ all.begin (), all.begin () + all.size () / rep_count };
	if (state.thread_index () == 0)
	{
		fixture.restart_elections ();
		for (auto const & vote : votes)
		{
			fixture.node->vote_router.vote (vote);
		}
	}
	allocation_counter allocations;
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, entry, pick (votes, state, index++));
	}
	allocations.report (state);
}

static void BM_vote_late (benchmark::State & state, entry_point entry)
{
	auto & fixture = shared_fixture ();
	auto const & votes = fixture.late_votes (state.range (0));
	allocation_counter allocations;
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, entry, pick (votes, state, index++));
	}
	allocations.report (state);
}

// clang-format off
BENCHMARK (BM_vote_validate)->ArgName ("hashes")->Arg (1)->Arg (16)->Arg (255)->Threads (1)->Threads (4)->UseRealTime ();

BENCHMARK_CAPTURE (BM_vote_unmatched, router, entry_point::router)->ArgName ("hashes")->Arg (1)->Arg (16)->Arg (255)->Threads (1)->Threads (4)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_unmatched, processor, entry_point::processor)->ArgName ("hashes")->Arg (16)->Threads (1)->Threads (4)->UseRealTime ();

BENCHMARK_CAPTURE (BM_vote_counted, router, entry_point::router)->ArgName ("hashes")->Arg (1)->Iterations (rep_count * election_count)->Threads (1)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_counted, router, entry_point::router)->ArgName ("hashes")->Arg (1)->Iterations (rep_count * election_count / 4)->Threads (4)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_counted, router, entry_point::router)->ArgName ("hashes")->Arg (16)->Iterations (rep_count * election_count / 16)->Threads (1)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_counted, router, entry_point::router)->ArgName ("hashes")->Arg (16)->Iterations (rep_count * election_count / 16 / 4)->Threads (4)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_counted, processor, entry_point::processor)->ArgName ("hashes")->Arg (16)->Iterations (rep_count * election_count / 16)->Threads (1)->UseRealTime ();

BENCHMARK_CAPTURE (BM_vote_replay, router, entry_point::router)->ArgName ("hashes")->Arg (1)->Arg (16)->Threads (1)->Threads (4)->UseRealTime ();
BENCHMARK_CAPTURE (BM_vote_late, router, entry_point::router)->ArgName ("hashes")->Arg (1)->Arg (16)->Threads (1)->Threads (4)->UseRealTime ();
// clang-format on
