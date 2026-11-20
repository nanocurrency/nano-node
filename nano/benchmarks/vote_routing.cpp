/*
 * Benchmarks of the vote router's own work as the number of live elections grows: looking a hash up in the route table, connecting an election and disconnecting it when it ends, and routing votes through the table to the elections.
 * Every benchmark reports the time and the heap allocations per operation, so a change to the table can be judged on both.
 *
 * The fixture is a dev network node whose router holds a route for each of `elections` live elections.
 * The elections are made directly around fabricated blocks and routed by hand, not through active elections, so the route table is the only thing that grows with the parameter.
 * They never confirm, the `rep_count` representatives stay below quorum together, so the first vote of every representative is counted.
 *
 * Scenarios:
 * - lookup: the route of a hash picked at random among the elections, or the miss for a hash without a route
 * - connect: a route is disconnected by hash and connected again, the per-hash path taken for fork blocks and the targets of votes
 * - turnover: an election is disconnected and connected again, what the router does when an election ends and another starts
 * - vote_replay: a vote of `vote_hashes` hashes the elections already hold, which the elections answer at once, so the router's share of the time is at its largest
 * - vote_counted: a vote of `vote_hashes` hashes the elections count, the full cost of a live vote; runs exactly once through the votes
 * - vote_late and vote_unmatched: votes for hashes without a route, answered from the recently confirmed cache or already held by the vote cache
 * - workload: replay votes with one election turned over every `votes_per_turnover` votes, about the ratio of a network where every principal representative votes once per election
 *
 * The reported time is the time one thread spends on one operation, the rate is the throughput of all threads together.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/block_sideband.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/epoch.hpp>
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
#include <nano/node/recently_confirmed_cache.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_context.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
constexpr size_t max_elections = 100'000; // The largest table the benchmarks ask for
constexpr size_t rep_count = 16;
constexpr size_t vote_hashes = 16;
constexpr size_t votes_per_turnover = 16;
constexpr size_t late_hashes = 32768;
constexpr size_t missing_hashes = 32768;

// Iterations per thread that deliver every counted vote of `count` elections exactly once
constexpr size_t counted_iterations (size_t count, size_t threads)
{
	return rep_count * (count / vote_hashes) / threads;
}

using context_list = std::vector<nano::vote_context>;

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

class routing_fixture
{
public:
	// An election with the block it holds, made on first use and kept when its route goes
	struct slot
	{
		std::shared_ptr<nano::block> block;
		nano::block_hash hash;
		std::shared_ptr<nano::election> election;
	};

	routing_fixture ()
	{
		auto config = system.default_config ();
		// Elections are made by hand, anything else starting them would make runs differ
		config.hinted_scheduler->enable = false;
		config.optimistic_scheduler->enable = false;
		config.priority_scheduler->enable = false;
		config.backlog_scan->enable = false;
		// The cache is emptied before elections restart, so no replay into them may overlap a measurement
		config.vote_cache_processor->enable = false;
		// Room for a vote per hash at the largest table, so the cache never evicts and costs the same at every size
		config.vote_cache->max_size = 2 * max_elections;
		node = system.add_node (config);
		channel = std::make_shared<nano::transport::inproc::channel> (*node, *node);

		for (size_t i = 0; i < rep_count; ++i)
		{
			reps.push_back (nano::test::setup_rep (system, *node, 100 * nano::Knano_ratio));
		}

		// The blocks never reach the ledger, an election only reads their hash, root, account and sideband
		nano::keypair const key;
		for (size_t i = 0; i < max_elections; ++i)
		{
			auto block = nano::block_builder{}
						 .state ()
						 .account (key.pub)
						 .previous (random_hash ())
						 .representative (key.pub)
						 .balance (1)
						 .link (0)
						 .sign_zero ()
						 .work (0)
						 .build ();
			block->sideband_set (nano::block_sideband{ key.pub, nano::amount{ 1 }, 2, 0, nano::epoch::epoch_0, false, false, false, nano::epoch::epoch_0 });
			elections.push_back ({ block, block->hash (), nullptr });
		}

		for (size_t i = 0; i < late_hashes; ++i)
		{
			late.push_back (random_hash ());
			node->active.recently_confirmed.put ({ random_hash (), random_hash () }, late.back (), nano::election_status{});
		}

		misses.resize (missing_hashes);
		std::ranges::generate (misses, random_hash);
	}

	nano::vote_router & router ()
	{
		return node->vote_router;
	}

	// Routes exactly the first `count` elections
	void resize (size_t count)
	{
		release_assert (count <= max_elections);
		for (; routed < count; ++routed)
		{
			auto & slot_l = elections[routed];
			if (!slot_l.election)
			{
				slot_l.election = make_election (slot_l);
			}
			router ().connect (slot_l.hash, slot_l.election);
		}
		while (routed > count)
		{
			--routed;
			auto const & slot_l = elections[routed];
			router ().disconnect (slot_l.hash, slot_l.election);
		}
	}

	// Gives the first `count` elections fresh ballots, routes exactly them and empties the vote cache
	void restart (size_t count)
	{
		resize (count);
		node->vote_cache.clear ();
		for (size_t i = 0; i < count; ++i)
		{
			auto & slot_l = elections[i];
			slot_l.election = make_election (slot_l);
			// The fresh election started later, so it takes the route over
			release_assert (router ().connect (slot_l.hash, slot_l.election));
		}
	}

	// Disconnects an election and connects it again, as when an election ends and another starts
	void turnover (size_t index)
	{
		auto const & slot_l = elections[index];
		router ().disconnect (slot_l.election);
		router ().connect (slot_l.hash, slot_l.election);
	}

	// Disconnects a route by hash and connects it again
	void reconnect (size_t index)
	{
		auto const & slot_l = elections[index];
		router ().disconnect (slot_l.hash, slot_l.election);
		router ().connect (slot_l.hash, slot_l.election);
	}

	// Indexes of the first `count` elections in a random order, so a run walks the table at random
	std::vector<size_t> const & order (size_t count)
	{
		std::lock_guard guard{ mutex };
		auto & order_l = orders[count];
		if (order_l.empty ())
		{
			order_l.resize (count);
			std::iota (order_l.begin (), order_l.end (), size_t{ 0 });
			std::shuffle (order_l.begin (), order_l.end (), std::mt19937{ static_cast<uint32_t> (count) });
		}
		return order_l;
	}

	// Hashes of the first `count` elections, in the random order
	std::vector<nano::block_hash> const & hashes (size_t count)
	{
		auto const & order_l = order (count);
		std::lock_guard guard{ mutex };
		auto & hashes_l = hashes_m[count];
		if (hashes_l.empty ())
		{
			for (auto index : order_l)
			{
				hashes_l.push_back (elections[index].hash);
			}
		}
		return hashes_l;
	}

	// Votes of the first representative on groups of `vote_hashes` elections among the first `count`, in the random order
	context_list const & replay_votes (size_t count)
	{
		auto const & hashes_l = hashes (count);
		std::lock_guard guard{ mutex };
		auto & votes = replay_votes_m[count];
		if (votes.empty ())
		{
			votes = grouped_votes (hashes_l, { reps.front () });
		}
		return votes;
	}

	// One vote per representative and group of `vote_hashes` elections among the first `count`, ordered by representative
	context_list const & counted_votes (size_t count)
	{
		auto const & hashes_l = hashes (count);
		std::lock_guard guard{ mutex };
		auto & votes = counted_votes_m[count];
		if (votes.empty ())
		{
			votes = grouped_votes (hashes_l, reps);
		}
		return votes;
	}

	// Votes on recently confirmed hashes
	context_list const & late_votes ()
	{
		std::lock_guard guard{ mutex };
		if (late_votes_m.empty ())
		{
			late_votes_m = grouped_votes (late, { reps.front () });
		}
		return late_votes_m;
	}

	// Votes on hashes without a route or confirmation, every representative votes on each group so the cache holds one entry per hash with `rep_count` voters
	context_list const & unmatched_votes ()
	{
		std::lock_guard guard{ mutex };
		if (unmatched_votes_m.empty ())
		{
			unmatched_votes_m = grouped_votes (misses, reps);
		}
		return unmatched_votes_m;
	}

public:
	dev_network network;
	nano::test::system system;
	std::shared_ptr<nano::node> node;
	std::shared_ptr<nano::transport::channel> channel;
	std::vector<nano::keypair> reps;
	std::vector<slot> elections; // `max_elections` slots, the first `routed` have a route
	std::vector<nano::block_hash> misses; // Hashes without a route or confirmation

private:
	std::shared_ptr<nano::election> make_election (slot const & slot_l) const
	{
		return std::make_shared<nano::election> (*node, slot_l.block, nano::election_behavior::priority);
	}

	// One vote per representative and group of `vote_hashes` consecutive hashes, ordered by representative, each with the context the vote processor would give it
	context_list grouped_votes (std::vector<nano::block_hash> const & hashes_l, std::vector<nano::keypair> const & voters) const
	{
		context_list votes;
		for (auto const & rep : voters)
		{
			for (size_t first = 0; first + vote_hashes <= hashes_l.size (); first += vote_hashes)
			{
				std::vector<nano::block_hash> group{ hashes_l.begin () + first, hashes_l.begin () + first + vote_hashes };
				auto vote = std::make_shared<nano::vote> (rep.pub, rep.prv, 1, 0, group);
				votes.push_back (node->vote_processor.context (vote, channel));
			}
		}
		return votes;
	}

	size_t routed{ 0 };
	std::mutex mutex; // Benchmark threads ask for their votes at the same time, the lists are built once under this lock
	std::vector<nano::block_hash> late;
	std::map<size_t, std::vector<size_t>> orders;
	std::map<size_t, std::vector<nano::block_hash>> hashes_m;
	std::map<size_t, context_list> replay_votes_m;
	std::map<size_t, context_list> counted_votes_m;
	context_list late_votes_m;
	context_list unmatched_votes_m;
};

// Built on first use, building it takes seconds
routing_fixture & shared_fixture ()
{
	static routing_fixture instance;
	return instance;
}

// Every thread walks its own part of a list, so threads never share an election
template <typename List>
auto const & pick (List const & list, benchmark::State const & state, size_t index)
{
	auto const part = list.size () / state.threads ();
	return list[state.thread_index () * part + index % part];
}

void deliver (routing_fixture & fixture, nano::vote_context const & context)
{
	benchmark::DoNotOptimize (fixture.router ().vote (context));
}

// Delivers every vote once, so the elections hold them before a measurement replays them
void deliver_all (routing_fixture & fixture, context_list const & votes)
{
	for (auto const & context : votes)
	{
		fixture.router ().vote (context);
	}
}

void election_counts (benchmark::internal::Benchmark * benchmark)
{
	benchmark->ArgName ("elections")->Arg (1'000)->Arg (5'000)->Arg (25'000)->Arg (100'000)->UseRealTime ();
}
}

// A lookup of the election for a hash picked at random among the live elections
static void BM_route_lookup (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & hashes = fixture.hashes (count);
	if (state.thread_index () == 0)
	{
		fixture.resize (count);
	}
	nano::benchmarks::allocation_counter allocations{ "lookup" };
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (fixture.router ().election (pick (hashes, state, index++)));
	}
	allocations.report (state);
}

// A lookup for a hash without a route
static void BM_route_lookup_miss (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	if (state.thread_index () == 0)
	{
		fixture.resize (state.range (0));
	}
	nano::benchmarks::allocation_counter allocations{ "lookup" };
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (fixture.router ().election (pick (fixture.misses, state, index++)));
	}
	allocations.report (state);
}

// A route disconnected by hash and connected again
static void BM_route_connect (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & order = fixture.order (count);
	if (state.thread_index () == 0)
	{
		fixture.resize (count);
	}
	nano::benchmarks::allocation_counter allocations{ "route" };
	size_t index = 0;
	for (auto _ : state)
	{
		fixture.reconnect (pick (order, state, index++));
	}
	allocations.report (state);
}

// An election disconnected and connected again
static void BM_route_turnover (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & order = fixture.order (count);
	if (state.thread_index () == 0)
	{
		fixture.resize (count);
	}
	nano::benchmarks::allocation_counter allocations{ "election" };
	size_t index = 0;
	for (auto _ : state)
	{
		fixture.turnover (pick (order, state, index++));
	}
	allocations.report (state);
}

static void BM_route_vote_replay (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & votes = fixture.replay_votes (count);
	if (state.thread_index () == 0)
	{
		fixture.restart (count);
		deliver_all (fixture, votes);
	}
	nano::benchmarks::allocation_counter allocations{ "vote" };
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, pick (votes, state, index++));
	}
	allocations.report (state);
}

// Runs exactly once through the votes, a second vote of a representative would not be counted
static void BM_route_vote_counted (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & votes = fixture.counted_votes (count);
	if (state.thread_index () == 0)
	{
		fixture.restart (count);
	}
	release_assert (state.max_iterations * state.threads () == votes.size ());
	nano::benchmarks::allocation_counter allocations{ "vote" };
	// Representatives are dealt out to the threads in turn, so every thread sees elections fill up the same way
	auto const groups = votes.size () / rep_count;
	size_t index = 0;
	for (auto _ : state)
	{
		auto const rep = state.thread_index () + state.threads () * (index / groups);
		deliver (fixture, votes[rep * groups + index % groups]);
		++index;
	}
	allocations.report (state);
}

static void BM_route_vote_late (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const & votes = fixture.late_votes ();
	if (state.thread_index () == 0)
	{
		fixture.resize (state.range (0));
	}
	nano::benchmarks::allocation_counter allocations{ "vote" };
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, pick (votes, state, index++));
	}
	allocations.report (state);
}

// The cache holds every vote before the measurement, so the time is the steady state of a hash nobody has an election for and the first fill is not amortized into it
static void BM_route_vote_unmatched (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const & votes = fixture.unmatched_votes ();
	if (state.thread_index () == 0)
	{
		fixture.resize (state.range (0));
		fixture.node->vote_cache.clear ();
		deliver_all (fixture, votes);
	}
	nano::benchmarks::allocation_counter allocations{ "vote" };
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, pick (votes, state, index++));
	}
	allocations.report (state);
}

// Replay votes with an election turned over every `votes_per_turnover` votes, each thread turning over its own part of the elections
static void BM_route_workload (benchmark::State & state)
{
	auto & fixture = shared_fixture ();
	auto const count = state.range (0);
	auto const & votes = fixture.replay_votes (count);
	auto const & order = fixture.order (count);
	if (state.thread_index () == 0)
	{
		fixture.restart (count);
		deliver_all (fixture, votes);
	}
	nano::benchmarks::allocation_counter allocations{ "vote" };
	size_t index = 0;
	for (auto _ : state)
	{
		deliver (fixture, pick (votes, state, index));
		if (index % votes_per_turnover == 0)
		{
			fixture.turnover (pick (order, state, index / votes_per_turnover));
		}
		++index;
	}
	allocations.report (state);
}

// clang-format off
BENCHMARK (BM_route_lookup)->Apply (election_counts);
BENCHMARK (BM_route_lookup_miss)->Apply (election_counts);
BENCHMARK (BM_route_connect)->Apply (election_counts);
BENCHMARK (BM_route_turnover)->Apply (election_counts);

BENCHMARK (BM_route_vote_replay)->Apply (election_counts)->Threads (1)->Threads (4);
BENCHMARK (BM_route_vote_counted)->ArgName ("elections")->Arg (1'000)->Iterations (counted_iterations (1'000, 1))->UseRealTime ();
BENCHMARK (BM_route_vote_counted)->ArgName ("elections")->Arg (5'000)->Iterations (counted_iterations (5'000, 1))->UseRealTime ();
BENCHMARK (BM_route_vote_counted)->ArgName ("elections")->Arg (25'000)->Iterations (counted_iterations (25'000, 1))->UseRealTime ();
BENCHMARK (BM_route_vote_counted)->ArgName ("elections")->Arg (100'000)->Iterations (counted_iterations (100'000, 1))->UseRealTime ();
BENCHMARK (BM_route_vote_late)->ArgName ("elections")->Arg (5'000)->UseRealTime ();
BENCHMARK (BM_route_vote_unmatched)->ArgName ("elections")->Arg (5'000)->UseRealTime ();
BENCHMARK (BM_route_workload)->Apply (election_counts)->Threads (1)->Threads (4);
// clang-format on
