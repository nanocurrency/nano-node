/*
 * Benchmarks of `nano::fair_queue` as the number of peers grows: the lookup a push takes, the seek a pop takes past the queues of idle peers, the scan a ready-only pop takes past the channels with no room, and the queue a peer gets on first contact.
 * Every benchmark reports the time and the heap allocations per request, so a change to the queue's containers or its cursor can be judged on both.
 *
 * The queue under test is keyed like the production ones, a source value plus a channel. The channel is a shared pointer to a polymorphic object whose `alive` and `max` cost what the tcp channel's do, an atomic load and a mutex acquisition behind a virtual call.
 * `peers` channels have a queue each, created in random order so the map's nodes lie in memory the way a node's do after peers came and went, not in key order.
 * Every request carries its channel, as every production request does, and is sized like a vote reply request, between the message processor's entry and the block processor's context.
 *
 * Profiles set the caps and the shares per turn:
 * - flat: every peer takes one request per turn under one cap, the message processor's queue; the vote replier and the bootstrap server queue the same way
 * - tiered: representatives take 3, 9 or 27 requests per turn by tier under the vote processor's caps, with a tier mix like a live node's peers
 * - reply: the vote replier's cap and batch size, for the ready scenario
 *
 * Scenarios:
 * - steady: one request arrives from a random peer and is popped before the next, the consumer keeping up; every pop seeks past the idle peers between the cursor and the sender
 * - backlog: `pending` requests from random peers are queued, then drained in one batch; the drain is one round over the ring, shared by the requests in it
 * - storm: every peer's queue is full and the whole lot is drained in the profile's batches; the time excludes the fill, the allocations include it
 * - ready: every peer holds requests and `saturated` percent of the channels have no room, so `next_ready_batch` skips them; the popped requests go back with `push_evicting`, so the state stays; at 100 percent every call scans the whole ring and comes back empty, what a reply thread does every capacity wait
 * - first_contact: a request from a peer the queue has never seen, which gets a queue of its own; every `contact_round` requests the newcomers have gone and a periodic update erases their queues, which the measurement includes
 * - periodic_update: the cleanup and refresh pass over every queue with nothing to erase, run every 30 s in production
 *
 * The message processor shards its peers over its threads, so its `peers` is one thread's share; the other owners see them all.
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/node/fair_queue.hpp>
#include <nano/secure/rep_tiers.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

using namespace std::chrono_literals;

namespace
{
std::vector<int64_t> const peer_counts{ 200, 500, 1000, 2000, 5000 };
std::vector<int64_t> const backlog_sizes{ 32, 256 }; // Requests queued between two drains
std::vector<int64_t> const saturated_percents{ 0, 50, 90, 99, 100 }; // Channels with no room in the ready scenario
constexpr size_t reply_pending = 4; // Requests every peer holds in the ready scenario
constexpr size_t contact_round = 1024; // First contacts between two periodic updates
constexpr size_t sender_count = 1 << 16; // Random senders drawn ahead of a run

/*
 * Stands in for a transport channel: `alive` is an atomic load and `max` a mutex acquisition, what the tcp channel's cost, both behind a virtual call
 */
class link
{
public:
	virtual ~link () = default;

	virtual bool alive () const = 0;
	// Whether the channel has no room for a reply
	virtual bool max () = 0;
};

class peer_link final : public link
{
public:
	bool alive () const override
	{
		return !closed;
	}

	bool max () override
	{
		std::lock_guard guard{ mutex };
		return saturated;
	}

	void close ()
	{
		closed = true;
	}

	void reopen ()
	{
		closed = false;
	}

	void saturate (bool value)
	{
		std::lock_guard guard{ mutex };
		saturated = value;
	}

private:
	std::mutex mutex;
	bool saturated{ false };
	std::atomic<bool> closed{ false };
};

// What one queued request carries: its channel and a payload the size of a vote reply request
struct request
{
	std::shared_ptr<link> channel;
	std::array<uint64_t, 3> payload{};
};

using queue_type = nano::fair_queue<request, nano::rep_tier, std::shared_ptr<link>>;
using origin_type = queue_type::origin_type;

// Caps and shares per turn of one production owner
struct queue_profile
{
	size_t pr_max_size; // Cap of a representative's queue
	size_t non_pr_max_size;
	size_t pr_priority; // Requests per turn grow by this power per tier, 1 for one per turn everywhere
	size_t batch_size;

	size_t max_size (nano::rep_tier tier) const
	{
		return tier == nano::rep_tier::none ? non_pr_max_size : pr_max_size;
	}

	size_t priority (nano::rep_tier tier) const
	{
		size_t result = 1;
		for (auto n = static_cast<size_t> (tier); n > 0; --n)
		{
			result *= pr_priority;
		}
		return result;
	}
};

constexpr queue_profile flat{ 64, 64, 1, 4096 }; // The message processor's
constexpr queue_profile tiered{ 256, 32, 3, 1024 }; // The vote processor's
constexpr queue_profile reply{ 128, 128, 1, 16 }; // The vote replier's

// Two percent of the peers are tier 3 representatives, eight percent tier 2, forty percent tier 1 and half are not representatives
nano::rep_tier tier_of (size_t index)
{
	auto const slot = index % 50;
	if (slot == 0)
	{
		return nano::rep_tier::tier_3;
	}
	if (slot < 5)
	{
		return nano::rep_tier::tier_2;
	}
	if (slot < 25)
	{
		return nano::rep_tier::tier_1;
	}
	return nano::rep_tier::none;
}

/*
 * `peers` channels with a tier each and a queue set up with the profile's caps and shares, every peer's queue created in random order and left empty
 */
class peer_fixture
{
public:
	peer_fixture (size_t peers, queue_profile const & profile_a) :
		profile{ profile_a }
	{
		configure (queue);

		std::mt19937_64 rng{ peers }; // Seeded by the size, so every run sees the same peers
		for (size_t i = 0; i < peers; ++i)
		{
			channels.push_back (std::make_shared<peer_link> ());
			tiers.push_back (tier_of (i));
		}
		for (size_t i = 0; i < sender_count; ++i)
		{
			senders.push_back (static_cast<uint32_t> (rng () % peers));
		}

		// Queues come into being in random order, so the map's nodes are not laid out in key order
		std::vector<size_t> order (peers);
		std::iota (order.begin (), order.end (), 0);
		std::shuffle (order.begin (), order.end (), rng);
		for (auto index : order)
		{
			release_assert (queue.push (make_request (index), origin (index)));
		}
		queue.next_batch (peers);
		release_assert (queue.empty ());
		release_assert (queue.queues_size () == peers);
	}

	// Sets the profile's caps and shares on a queue
	void configure (queue_type & queue_a) const
	{
		queue_a.max_size_query = [this] (origin_type const & origin) {
			return profile.max_size (origin.source);
		};
		queue_a.priority_query = [this] (origin_type const & origin) {
			return profile.priority (origin.source);
		};
		queue_a.alive_query = [] (origin_type const & origin) {
			return origin.channel->alive ();
		};
	}

	size_t peers () const
	{
		return channels.size ();
	}

	origin_type origin (size_t index) const
	{
		return { tiers[index], channels[index] };
	}

	request make_request (size_t index) const
	{
		return { channels[index] };
	}

	// The index of the peer sending the n-th request
	size_t sender (size_t n) const
	{
		return senders[n % senders.size ()];
	}

	peer_link & control (size_t index) const
	{
		return static_cast<peer_link &> (*channels[index]);
	}

	// Queues `count` requests under every peer
	void fill (size_t count)
	{
		for (size_t index = 0; index < peers (); ++index)
		{
			for (size_t n = 0; n < count; ++n)
			{
				release_assert (queue.push (make_request (index), origin (index)));
			}
		}
	}

	// Queues every peer's cap of requests
	void fill_to_cap ()
	{
		for (size_t index = 0; index < peers (); ++index)
		{
			auto const cap = profile.max_size (tiers[index]);
			for (size_t n = 0; n < cap; ++n)
			{
				release_assert (queue.push (make_request (index), origin (index)));
			}
		}
	}

	// Leaves `percent` of the channels with no room, spread over the ring since the peers are in random key order
	void saturate (size_t percent)
	{
		for (size_t index = 0; index < peers (); ++index)
		{
			control (index).saturate (index * 100 < peers () * percent);
		}
	}

public:
	queue_profile const & profile;
	queue_type queue;
	std::vector<std::shared_ptr<link>> channels;
	std::vector<nano::rep_tier> tiers;
	std::vector<uint32_t> senders;
};

size_t peers_of (benchmark::State const & state)
{
	return static_cast<size_t> (state.range (0));
}

void peer_args (benchmark::internal::Benchmark * benchmark)
{
	benchmark->ArgName ("peers");
	for (auto peers : peer_counts)
	{
		benchmark->Arg (peers);
	}
}
}

// One request at a time from a random peer, popped before the next arrives
static void BM_fair_queue_steady (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	nano::benchmarks::allocation_counter allocations{ "request" };
	size_t index = 0;
	for (auto _ : state)
	{
		auto const sender = fixture.sender (index++);
		fixture.queue.push (fixture.make_request (sender), fixture.origin (sender));
		auto batch = fixture.queue.next_batch (profile.batch_size);
		benchmark::DoNotOptimize (batch);
	}
	allocations.report (state);
}

// `pending` requests from random peers queued, then drained in one batch
static void BM_fair_queue_backlog (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	auto const pending = static_cast<size_t> (state.range (1));
	nano::benchmarks::allocation_counter allocations{ "request" };
	size_t index = 0;
	uint64_t popped = 0;
	for (auto _ : state)
	{
		for (size_t n = 0; n < pending; ++n)
		{
			auto const sender = fixture.sender (index++);
			fixture.queue.push (fixture.make_request (sender), fixture.origin (sender));
		}

		auto batch = fixture.queue.next_batch (profile.batch_size);
		benchmark::DoNotOptimize (batch);
		popped += batch.size ();
	}
	allocations.report (state, popped);
}

// Every peer's queue full, drained in the profile's batches until empty
static void BM_fair_queue_storm (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	nano::benchmarks::allocation_counter allocations{ "request" };
	uint64_t popped = 0;
	for (auto _ : state)
	{
		state.PauseTiming ();
		fixture.fill_to_cap ();
		state.ResumeTiming ();

		while (!fixture.queue.empty ())
		{
			auto batch = fixture.queue.next_batch (profile.batch_size);
			benchmark::DoNotOptimize (batch);
			popped += batch.size ();
		}
	}
	allocations.report (state, popped);
}

// Every peer holds requests and `saturated` percent of the channels have no room; a batch of the ready ones is popped and pushed back
static void BM_fair_queue_ready (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	fixture.saturate (static_cast<size_t> (state.range (1)));
	fixture.fill (reply_pending);
	// A dead channel counts as ready, as the reply servers have it
	auto const ready = [] (origin_type const & origin) {
		return !origin.channel->alive () || !origin.channel->max ();
	};
	nano::benchmarks::allocation_counter allocations{ "request" };
	uint64_t popped = 0;
	for (auto _ : state)
	{
		auto batch = fixture.queue.next_ready_batch (profile.batch_size, ready);
		popped += batch.size ();
		for (auto & [request, origin] : batch)
		{
			fixture.queue.push_evicting (std::move (request), origin);
		}
	}
	// With every channel full nothing is popped and the time per iteration is the scan alone
	if (popped > 0)
	{
		allocations.report (state, popped);
	}
}

// A request from a peer the queue has never seen; every `contact_round` requests the newcomers have gone and a periodic update erases their queues
static void BM_fair_queue_first_contact (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	std::vector<std::shared_ptr<peer_link>> newcomers;
	for (size_t n = 0; n < contact_round; ++n)
	{
		newcomers.push_back (std::make_shared<peer_link> ());
	}

	nano::benchmarks::allocation_counter allocations{ "request" };
	size_t index = 0;
	for (auto _ : state)
	{
		auto const slot = index++ % contact_round;
		if (slot == 0 && index > 1)
		{
			// The round's newcomers go, their empty queues are erased and the same channels come back as strangers
			for (auto & newcomer : newcomers)
			{
				newcomer->close ();
			}
			auto const updated = fixture.queue.periodic_update (0ms);
			release_assert (updated);
			release_assert (fixture.queue.queues_size () == fixture.peers ());
			for (auto & newcomer : newcomers)
			{
				newcomer->reopen ();
			}
		}

		auto const & newcomer = newcomers[slot];
		fixture.queue.push (request{ newcomer }, origin_type{ nano::rep_tier::none, newcomer });
		auto batch = fixture.queue.next_batch (profile.batch_size);
		benchmark::DoNotOptimize (batch);
	}
	allocations.report (state);
}

// The cleanup and refresh pass over the queues of `peers` live peers, with nothing to erase
static void BM_fair_queue_periodic_update (benchmark::State & state, queue_profile const & profile)
{
	peer_fixture fixture{ peers_of (state), profile };
	nano::benchmarks::allocation_counter allocations{ "update" };
	for (auto _ : state)
	{
		auto const updated = fixture.queue.periodic_update (0ms);
		release_assert (updated);
	}
	allocations.report (state);
}

// clang-format off
BENCHMARK_CAPTURE (BM_fair_queue_steady, flat, flat)->Apply (peer_args);
BENCHMARK_CAPTURE (BM_fair_queue_steady, tiered, tiered)->Apply (peer_args);
BENCHMARK_CAPTURE (BM_fair_queue_backlog, flat, flat)->ArgNames ({ "peers", "pending" })->ArgsProduct ({ peer_counts, backlog_sizes });
BENCHMARK_CAPTURE (BM_fair_queue_backlog, tiered, tiered)->ArgNames ({ "peers", "pending" })->ArgsProduct ({ peer_counts, backlog_sizes });
BENCHMARK_CAPTURE (BM_fair_queue_storm, flat, flat)->Apply (peer_args);
BENCHMARK_CAPTURE (BM_fair_queue_storm, tiered, tiered)->Apply (peer_args);
BENCHMARK_CAPTURE (BM_fair_queue_ready, reply, reply)->ArgNames ({ "peers", "saturated" })->ArgsProduct ({ peer_counts, saturated_percents });
BENCHMARK_CAPTURE (BM_fair_queue_first_contact, flat, flat)->Apply (peer_args);
BENCHMARK_CAPTURE (BM_fair_queue_periodic_update, flat, flat)->Apply (peer_args);
// clang-format on
