/*
 * Benchmarks of the queue a tcp channel holds its outbound messages in: what queuing a message, popping the sender's batches and asking whether a traffic type is at its limit cost.
 * Every benchmark reports the time and the heap allocations per message, so a change to the queue's containers or its scheduling can be judged on both.
 *
 * The queue sits behind a mutex the way it does in the channel: a send queues under it, the sender pops a batch of `send_batch` under it and hands the messages on outside it.
 * Every message carries one shared buffer the size of a vote and no callback, as a flood's messages do.
 *
 * Mixes:
 * - vote: every message is a rebroadcast vote, the one traffic type of a channel that only relays votes
 * - mixed: messages spread over the traffic types a live channel carries, weighted toward votes
 *
 * Scenarios:
 * - trickle: one message is queued and the sender pops it before the next arrives, the channel keeping up
 * - burst: `count` messages are queued, then the sender pops them in its batches
 * - saturated: every traffic type is at its cap; every iteration pops one batch and queues the same types again, so the queue stays full
 * - max: the limit check a flood makes per channel and traffic type, against a queue holding a burst
 * - lifecycle: a fresh queue takes one message of every traffic type, gives them up and is destroyed; every allocation of that life is counted, including any freed along the way, so it is allocation traffic and not memory held
 *
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/benchmarks/allocations.hpp>
#include <nano/lib/asio.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/traffic_type.hpp>

#include <algorithm>
#include <mutex>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

namespace
{
using nano::transport::traffic_type;

constexpr size_t send_batch = nano::transport::traffic_queue::batch_capacity; // Messages the channel's sender pops at a time
constexpr size_t draw_count = 1 << 16; // Traffic types drawn ahead of a run
constexpr size_t max_backlog = 64; // Messages queued for the max scenario

using queue_type = nano::transport::traffic_queue;

enum class traffic_mix
{
	vote,
	mixed,
};

// The traffic types of `draw_count` messages in the mix, drawn with a fixed seed so every run sees the same sequence
std::vector<traffic_type> draw_types (traffic_mix mix)
{
	if (mix == traffic_mix::vote)
	{
		return std::vector<traffic_type> (draw_count, traffic_type::vote_rebroadcast);
	}

	// Percent of a channel's messages per traffic type
	std::vector<std::pair<traffic_type, unsigned>> const weights{
		{ traffic_type::vote_rebroadcast, 35 },
		{ traffic_type::vote_normal, 10 },
		{ traffic_type::vote_final, 10 },
		{ traffic_type::vote_reply, 10 },
		{ traffic_type::block_broadcast, 10 },
		{ traffic_type::block_rebroadcast, 5 },
		{ traffic_type::confirmation_requests, 10 },
		{ traffic_type::bootstrap, 4 },
		{ traffic_type::bootstrap_server, 3 },
		{ traffic_type::keepalive, 1 },
		{ traffic_type::telemetry, 1 },
		{ traffic_type::rep_crawler, 1 },
	};
	std::vector<unsigned> cumulative;
	unsigned total = 0;
	for (auto const & [type, weight] : weights)
	{
		total += weight;
		cumulative.push_back (total);
	}

	std::mt19937_64 rng{ 42 };
	std::vector<traffic_type> result;
	result.reserve (draw_count);
	for (size_t n = 0; n < draw_count; ++n)
	{
		auto const pick = static_cast<unsigned> (rng () % total);
		auto const index = std::upper_bound (cumulative.begin (), cumulative.end (), pick) - cumulative.begin ();
		result.push_back (weights[index].first);
	}
	return result;
}

// The drawn traffic types of the mix, the same sequence for every fixture
std::vector<traffic_type> const & traffic_types (traffic_mix mix)
{
	static std::vector<traffic_type> const vote = draw_types (traffic_mix::vote);
	static std::vector<traffic_type> const mixed = draw_types (traffic_mix::mixed);
	return mix == traffic_mix::vote ? vote : mixed;
}

// The serialized message every send queues, about the size of a vote
nano::shared_const_buffer const & message_buffer ()
{
	static nano::shared_const_buffer const buffer{ std::vector<uint8_t> (200) };
	return buffer;
}

/*
 * A channel's side of the queue: sends queue under its mutex, the sender pops its batches under the same mutex and hands them on outside it
 */
class channel_fixture
{
public:
	explicit channel_fixture (traffic_mix mix) :
		types{ traffic_types (mix) }
	{
		message_buffer (); // Made up front like the draws, so the counters see only the queue's allocations
	}

	// Queues a message of the traffic type, false when the type is full and the message was dropped
	bool send (traffic_type type)
	{
		std::lock_guard guard{ mutex };
		return !queue.full (type) && queue.push ({ message_buffer (), nullptr }, type);
	}

	// Pops one batch the way the sender does and hands each message with its traffic type to the visitor, returns how many it popped
	template <typename Visitor>
	size_t drain_batch (Visitor && visit)
	{
		auto batch = [this] () {
			std::lock_guard guard{ mutex };
			return queue.next_batch (send_batch);
		}();
		for (auto & [entry, type] : batch)
		{
			visit (type, entry);
		}
		return batch.size ();
	}

	size_t drain_batch ()
	{
		return drain_batch ([] (traffic_type, auto const & entry) {
			benchmark::DoNotOptimize (entry);
		});
	}

	// Pops batches until the queue is empty, returns how many messages it popped
	size_t drain_all ()
	{
		size_t total = 0;
		while (auto const popped = drain_batch ())
		{
			total += popped;
		}
		return total;
	}

	bool max (traffic_type type)
	{
		std::lock_guard guard{ mutex };
		return queue.size (type) >= nano::transport::tcp_channel::max_queue_size;
	}

	// The traffic type of the next message in the mix
	traffic_type draw ()
	{
		return types[next_draw++ % types.size ()];
	}

public:
	std::mutex mutex;
	queue_type queue{ [] (traffic_type) { return nano::transport::tcp_channel::full_queue_size; }, nano::transport::traffic_priority }; // Set up as the channel sets up its own

private:
	std::vector<traffic_type> const & types;
	size_t next_draw{ 0 };
};

// What a queue and a popped batch take inline, the batch living in the sender's coroutine frame
void report_sizes (benchmark::State & state)
{
	state.counters["queue_bytes"] = sizeof (queue_type);
	state.counters["batch_bytes"] = sizeof (decltype (std::declval<queue_type &> ().next_batch (send_batch)));
}
}

// One message at a time, popped before the next is queued
static void BM_channel_queue_trickle (benchmark::State & state, traffic_mix mix)
{
	channel_fixture fixture{ mix };
	nano::benchmarks::allocation_counter allocations{ "message" };
	for (auto _ : state)
	{
		fixture.send (fixture.draw ());
		fixture.drain_batch ();
	}
	allocations.report (state);
	report_sizes (state);
}

// `count` messages queued, then popped in the sender's batches
static void BM_channel_queue_burst (benchmark::State & state, traffic_mix mix)
{
	channel_fixture fixture{ mix };
	auto const count = static_cast<size_t> (state.range (0));
	nano::benchmarks::allocation_counter allocations{ "message" };
	uint64_t popped = 0;
	for (auto _ : state)
	{
		for (size_t n = 0; n < count; ++n)
		{
			fixture.send (fixture.draw ());
		}
		popped += fixture.drain_all ();
	}
	allocations.report (state, popped);
}

// Every traffic type at its cap; one batch is popped and the same types are queued again
static void BM_channel_queue_saturated (benchmark::State & state)
{
	channel_fixture fixture{ traffic_mix::mixed };
	for (auto type : nano::transport::all_traffic_types ())
	{
		while (fixture.send (type))
		{
		}
	}

	std::vector<traffic_type> popped_types;
	popped_types.reserve (send_batch);

	nano::benchmarks::allocation_counter allocations{ "message" };
	uint64_t popped = 0;
	for (auto _ : state)
	{
		popped_types.clear ();
		fixture.drain_batch ([&popped_types] (traffic_type type, auto const & entry) {
			benchmark::DoNotOptimize (entry);
			popped_types.push_back (type);
		});
		for (auto type : popped_types)
		{
			release_assert (fixture.send (type));
		}
		popped += popped_types.size ();
	}
	allocations.report (state, popped);
}

// The limit check of one traffic type against a queue holding a burst of the mix
static void BM_channel_queue_max (benchmark::State & state)
{
	channel_fixture fixture{ traffic_mix::mixed };
	for (size_t n = 0; n < max_backlog; ++n)
	{
		fixture.send (fixture.draw ());
	}

	auto const & types = nano::transport::all_traffic_types ();
	nano::benchmarks::allocation_counter allocations{ "check" };
	size_t index = 0;
	for (auto _ : state)
	{
		benchmark::DoNotOptimize (fixture.max (types[index++ % types.size ()]));
	}
	allocations.report (state);
}

// A fresh queue takes one message of every traffic type, gives them up and is destroyed, every allocation of that life counted
static void BM_channel_queue_lifecycle (benchmark::State & state)
{
	// The shared draws and buffer are made before counting, so only the queue's own allocations are seen
	traffic_types (traffic_mix::vote);
	message_buffer ();

	nano::benchmarks::allocation_counter allocations{ "queue" };
	for (auto _ : state)
	{
		std::optional<channel_fixture> fixture;
		fixture.emplace (traffic_mix::vote);
		for (auto type : nano::transport::all_traffic_types ())
		{
			fixture->send (type);
		}
		fixture->drain_all ();
		benchmark::DoNotOptimize (fixture);
	}
	allocations.report (state);
	report_sizes (state);
}

// clang-format off
BENCHMARK_CAPTURE (BM_channel_queue_trickle, vote, traffic_mix::vote);
BENCHMARK_CAPTURE (BM_channel_queue_trickle, mixed, traffic_mix::mixed);
BENCHMARK_CAPTURE (BM_channel_queue_burst, vote, traffic_mix::vote)->ArgName ("count")->Arg (8)->Arg (64)->Arg (128);
BENCHMARK_CAPTURE (BM_channel_queue_burst, mixed, traffic_mix::mixed)->ArgName ("count")->Arg (8)->Arg (64)->Arg (256);
BENCHMARK (BM_channel_queue_saturated);
BENCHMARK (BM_channel_queue_max);
BENCHMARK (BM_channel_queue_lifecycle);
// clang-format on
