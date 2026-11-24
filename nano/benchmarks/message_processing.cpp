/*
 * Benchmarks of `nano::message_processor` under the load the io threads put on it: messages from many channels queued and dispatched on its threads.
 * `producer_count` threads hand keepalives over on `channel_count` channels, a few per channel in a row as a peer sends them, to a processor of their own with `threads` processing threads and the production cap of messages per channel.
 *
 * The processor dispatches to the benchmark's own dispatcher, so no handler runs: a keepalive, the cheapest message to build, carries its channel and its position in its first peer, and the dispatcher records when it arrived.
 * Every message's wait in the queue is known from that, and so is every message dispatched after a later one of its channel, which `reorder_pct` counts.
 *
 * Families:
 * - burst: every channel has a full queue before the processor starts, as a storm fills the queues, and the drain is timed; every burst gets a fresh processor
 * - paced: keepalives arrive at a steady offered rate for `paced_duration` with the processor running
 * - hot: paced at `hot_rate` with half of the keepalives from one channel
 *
 * Every run reports items/s dispatched, the queue wait as p50, p99 and max in µs, drop_pct (refused by a full channel queue), reorder_pct, and offered_per_s for a paced run, the rate the producers reached, or put_ns for a burst, what a hand-over costs the delivering thread.
 * The hot family reports the wait and the drops of the hot channel and of the others apart.
 *
 * Record a run with --benchmark_out=<file> --benchmark_out_format=json and compare two of them with submodules/benchmark/tools/compare.py
 */

#include <nano/lib/config.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/node/message_dispatcher.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/system.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

using namespace std::chrono_literals;
using std::chrono::steady_clock;

namespace
{
constexpr size_t channel_count = 256; // About the peer count on the live network
constexpr size_t producer_count = 4; // Threads handing messages over, as the io threads do
constexpr size_t run_length = 4; // Keepalives a channel hands over in a row before its producer moves on, as a peer sends a few at a time
constexpr size_t burst_rounds = 16;
constexpr std::chrono::milliseconds paced_duration{ 500 };
constexpr std::array<uint64_t, 3> paced_rates{ 200'000, 1'000'000, 4'000'000 };
constexpr uint64_t hot_rate = 1'000'000;
constexpr unsigned hot_percent = 50;
constexpr std::array<int64_t, 3> thread_counts{ 1, 2, 4 };

// The benchmarks' main leaves the network alone, a node must never start on another one from here
struct dev_network
{
	dev_network ()
	{
		nano::force_nano_dev_network ();
	}
};

// The node the channels and the stats belong to
class processor_fixture
{
public:
	processor_fixture ()
	{
		node = system.add_node (system.default_config ());
	}

public:
	dev_network network;
	nano::test::system system;
	std::shared_ptr<nano::node> node;
};

// Built on first use, starting the node takes a moment
processor_fixture & shared_fixture ()
{
	static processor_fixture instance;
	return instance;
}

int64_t since_epoch (steady_clock::time_point time)
{
	return time.time_since_epoch ().count ();
}

// Where a message comes from and which of its channel's messages it is, carried in the first peer of a keepalive: the channel in the port, the position in the address
struct message_id
{
	uint32_t channel;
	uint64_t position;
};

std::unique_ptr<nano::messages::keepalive> make_keepalive (nano::node & node, message_id id)
{
	boost::asio::ip::address_v6::bytes_type bytes{};
	std::memcpy (bytes.data (), &id.position, sizeof (id.position));
	auto message = std::make_unique<nano::messages::keepalive> (node.network_params.network);
	message->peers[0] = nano::endpoint{ boost::asio::ip::address_v6{ bytes }, static_cast<uint16_t> (id.channel) };
	return message;
}

message_id id_of (nano::messages::message const & message)
{
	debug_assert (message.type () == nano::messages::message_type::keepalive);
	auto const & peer = static_cast<nano::messages::keepalive const &> (message).peers[0];
	auto const bytes = peer.address ().to_v6 ().to_bytes ();
	message_id id{ peer.port (), 0 };
	std::memcpy (&id.position, bytes.data (), sizeof (id.position));
	return id;
}

/*
 * Records when every message of a run was handed over and when it was dispatched, and how often a message was dispatched after a later one of its channel.
 * A message is its channel and its position among the channel's accepted messages.
 */
class sink final
{
public:
	// `per_channel` is the most messages a channel hands over during the run
	explicit sink (std::vector<size_t> const & per_channel) :
		channels (per_channel.size ())
	{
		first.push_back (0);
		for (auto count : per_channel)
		{
			first.push_back (first.back () + count);
		}
		enqueued_ns.assign (first.back (), 0);
		latency_ns.assign (first.back (), 0);
	}

	// Waits count from this moment for the messages handed over earlier, the start of a drain
	void set_baseline (steady_clock::time_point time)
	{
		baseline.store (since_epoch (time), std::memory_order_release);
	}

	// The message is about to be handed over
	void handed_over (message_id id, steady_clock::time_point time)
	{
		enqueued_ns[index_of (id)] = since_epoch (time);
	}

	// The processor dispatched the message
	void dispatched (message_id id, steady_clock::time_point time)
	{
		auto const index = index_of (id);
		auto const waited = since_epoch (time) - std::max (enqueued_ns[index], baseline.load (std::memory_order_relaxed));
		latency_ns[index] = static_cast<uint32_t> (std::clamp<int64_t> (waited, 1, std::numeric_limits<uint32_t>::max ()));

		// A message dispatched behind a later one of its channel was overtaken
		auto & highest = channels[id.channel].highest;
		auto const done = id.position + 1;
		auto previous = highest.load (std::memory_order_relaxed);
		while (previous < done && !highest.compare_exchange_weak (previous, done, std::memory_order_relaxed))
		{
		}
		if (previous > done)
		{
			reorders_m.fetch_add (1, std::memory_order_relaxed);
		}
		dispatched_m.fetch_add (1, std::memory_order_release);
	}

	uint64_t dispatched () const
	{
		return dispatched_m.load (std::memory_order_acquire);
	}

	uint64_t reorders () const
	{
		return reorders_m.load (std::memory_order_relaxed);
	}

	size_t index_of (message_id id) const
	{
		return first[id.channel] + id.position;
	}

	// Nanoseconds a message waited in the queue, by index, 0 for one that was never dispatched
	std::vector<uint32_t> const & latencies () const
	{
		return latency_ns;
	}

private:
	// Padded so that the threads dispatching different channels do not share a cache line
	struct alignas (64) channel_state
	{
		std::atomic<uint64_t> highest{ 0 }; // Highest position dispatched so far, plus one
	};

	std::vector<size_t> first; // Index of a channel's first message, one past the last channel at the end
	std::atomic<int64_t> baseline{ 0 };
	std::vector<int64_t> enqueued_ns;
	std::vector<uint32_t> latency_ns;
	std::vector<channel_state> channels;
	std::atomic<uint64_t> dispatched_m{ 0 };
	std::atomic<uint64_t> reorders_m{ 0 };
};

// Takes what the processor dispatches and tells the sink which message it was
class bench_dispatcher final : public nano::message_dispatcher
{
public:
	explicit bench_dispatcher (sink & sink_a) :
		target{ sink_a }
	{
	}

	void dispatch (nano::messages::message const & message, std::shared_ptr<nano::transport::channel> const &) override
	{
		target.dispatched (id_of (message), steady_clock::now ());
	}

private:
	sink & target;
};

/*
 * What a run offers. With a rate the producers pace themselves and one processor runs throughout.
 * Without one, every round is handed over as fast as possible to a fresh processor that has not started, and that processor then runs until the round is drained.
 */
struct workload
{
	size_t rounds{ 1 }; // Bursts, each drained before the next one starts
	size_t messages{ 0 }; // Messages per round over all channels
	uint64_t rate{ 0 }; // Messages per second offered over all producers, 0 for as fast as possible
	unsigned hot_percent{ 0 }; // Share of the messages that go to channel 0, 0 for an even spread
};

// What one producer sends: the channel of each of its messages for all rounds in order, the same count every round
struct producer_plan
{
	std::vector<uint32_t> channels;
	size_t per_round{ 0 };
};

// Messages per channel and round
std::vector<size_t> channel_budget (workload const & load)
{
	std::vector<size_t> budget (channel_count, load.messages / channel_count);
	if (load.hot_percent > 0)
	{
		size_t const cold_each = load.messages * (100 - load.hot_percent) / 100 / (channel_count - 1);
		std::fill (budget.begin (), budget.end (), cold_each);
		budget[0] = load.messages - cold_each * (channel_count - 1);
	}
	return budget;
}

// Deals the messages of a round out to the producers, each walking its channels in turn, a run of messages per channel, so channels interleave as they do on the io threads
std::vector<producer_plan> make_plans (workload const & load)
{
	auto const budget = channel_budget (load);
	// Hot messages per message of a cold channel, so the hot channel's share is spread over the round
	size_t const hot_weight = load.hot_percent > 0 ? (budget[0] + budget[1] - 1) / budget[1] : 1;

	std::vector<producer_plan> plans (producer_count);
	for (size_t producer = 0; producer < producer_count; ++producer)
	{
		auto & plan = plans[producer];
		size_t const first = producer * channel_count / producer_count;
		size_t const last = (producer + 1) * channel_count / producer_count;

		for (size_t round = 0; round < load.rounds; ++round)
		{
			std::vector<size_t> remaining (budget.begin (), budget.end ());
			bool more = true;
			while (more)
			{
				more = false;
				for (size_t channel = first; channel < last; ++channel)
				{
					auto const weight = (channel == 0 ? hot_weight : 1) * run_length;
					for (size_t i = 0; i < weight && remaining[channel] > 0; ++i)
					{
						plan.channels.push_back (static_cast<uint32_t> (channel));
						--remaining[channel];
					}
					more |= remaining[channel] > 0;
				}
			}
		}
		plan.per_round = plan.channels.size () / load.rounds;
	}
	return plans;
}

// Padded so that the producers do not share a cache line
struct alignas (64) producer_stats
{
	std::atomic<uint64_t> dropped_hot{ 0 }; // Messages of channel 0 the processor refused
	std::atomic<uint64_t> dropped_cold{ 0 }; // Messages of the other channels it refused
	std::chrono::nanoseconds busy{ 0 }; // Time spent handing messages over, pacing included
	std::vector<size_t> accepted = std::vector<size_t> (channel_count, 0); // Messages the processor took, by channel
};

// Lets the producers start together and holds them between rounds until the processor of the round is in place and the previous round is drained
struct run_control
{
	std::atomic<nano::message_processor *> processor{ nullptr }; // Processor of the current round
	std::atomic<size_t> finished{ 0 }; // Producer rounds finished so far
	std::atomic<size_t> released{ 0 }; // Rounds the producers may go into

	void wait_round (size_t round) const
	{
		while (released.load (std::memory_order_acquire) <= round)
		{
			std::this_thread::yield ();
		}
	}
};

// Hands the plan's messages over, paced to the producer's share of the offered rate
void produce (nano::node & node, run_control & control, std::vector<std::shared_ptr<nano::transport::channel>> const & channels, producer_plan const & plan, workload const & load, sink & target, producer_stats & stats)
{
	std::chrono::nanoseconds interval{ 0 };
	if (load.rate > 0)
	{
		interval = std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::duration<double> (static_cast<double> (load.messages) / (static_cast<double> (load.rate) * plan.per_round)));
	}

	for (size_t round = 0; round < load.rounds; ++round)
	{
		control.wait_round (round);
		auto & processor = *control.processor.load (std::memory_order_acquire);

		auto const round_start = steady_clock::now ();
		for (size_t i = 0; i < plan.per_round; ++i)
		{
			auto const channel = plan.channels[round * plan.per_round + i];

			if (interval > 0ns)
			{
				auto const due = round_start + interval * i;
				while (steady_clock::now () < due)
				{
				}
			}

			// A refused message gives its position to the next one, so the accepted messages of a channel are numbered without gaps
			message_id const id{ channel, stats.accepted[channel] };
			auto message = make_keepalive (node, id);
			target.handed_over (id, steady_clock::now ());
			if (processor.enqueue (std::move (message), channels[channel]))
			{
				++stats.accepted[channel];
			}
			else
			{
				auto & dropped = channel == 0 ? stats.dropped_hot : stats.dropped_cold;
				dropped.fetch_add (1, std::memory_order_relaxed);
			}
		}
		stats.busy += steady_clock::now () - round_start;
		control.finished.fetch_add (1, std::memory_order_acq_rel);
	}
}

struct latency_summary
{
	double p50_us{ 0 };
	double p99_us{ 0 };
	double max_us{ 0 };
};

latency_summary summarize (std::vector<uint32_t> & values)
{
	latency_summary summary;
	if (values.empty ())
	{
		return summary;
	}
	auto const percentile = [&values] (size_t numerator, size_t denominator) {
		auto const position = std::min (values.size () - 1, values.size () * numerator / denominator);
		std::nth_element (values.begin (), values.begin () + position, values.end ());
		return values[position] / 1000.0;
	};
	summary.p50_us = percentile (50, 100);
	summary.p99_us = percentile (99, 100);
	summary.max_us = *std::max_element (values.begin (), values.end ()) / 1000.0;
	return summary;
}

struct scenario_result
{
	std::chrono::nanoseconds elapsed{ 0 }; // Time the processor was running, until the last message of a round was dispatched
	std::chrono::nanoseconds busy{ 0 }; // Sum of the producers' hand-over time
	uint64_t offered{ 0 };
	uint64_t dispatched{ 0 };
	uint64_t dropped{ 0 };
	uint64_t dropped_hot{ 0 };
	uint64_t offered_hot{ 0 };
	uint64_t reorders{ 0 };
	latency_summary all;
	latency_summary hot;
	latency_summary cold;
};

uint64_t dropped_total (std::vector<producer_stats> const & stats)
{
	uint64_t total = 0;
	for (auto const & producer : stats)
	{
		total += producer.dropped_hot.load (std::memory_order_relaxed) + producer.dropped_cold.load (std::memory_order_relaxed);
	}
	return total;
}

// Runs the workload once against processors with `threads` threads and gathers what happened to every message
scenario_result run_scenario (size_t threads, workload const & load)
{
	auto & node = *shared_fixture ().node;
	auto const plans = make_plans (load);
	auto per_channel = channel_budget (load);
	for (auto & count : per_channel)
	{
		count *= load.rounds;
	}
	uint64_t const total = std::accumulate (per_channel.begin (), per_channel.end (), uint64_t{ 0 });

	sink target{ per_channel };
	bench_dispatcher dispatcher{ target };
	std::vector<std::shared_ptr<nano::transport::channel>> channels;
	for (uint32_t i = 0; i < channel_count; ++i)
	{
		channels.push_back (std::make_shared<nano::transport::fake::channel> (node));
	}
	std::vector<producer_stats> stats (producer_count);
	run_control control;
	bool const paced = load.rate > 0;

	nano::message_processor_config config;
	config.threads = threads;

	// Drained: everything handed over so far was either dispatched or refused
	auto const drained = [&] (uint64_t offered) {
		return target.dispatched () + dropped_total (stats) >= offered;
	};

	scenario_result result;
	std::vector<std::thread> producers;
	for (size_t i = 0; i < producer_count; ++i)
	{
		producers.emplace_back (produce, std::ref (node), std::ref (control), std::cref (channels), std::cref (plans[i]), std::cref (load), std::ref (target), std::ref (stats[i]));
	}

	uint64_t offered = 0;
	for (size_t round = 0; round < load.rounds; ++round)
	{
		nano::message_processor processor{ config, dispatcher, node.stats, node.logger };
		control.processor.store (&processor, std::memory_order_release);
		if (paced)
		{
			target.set_baseline (steady_clock::now ());
			processor.start ();
		}
		auto const begin = steady_clock::now ();
		control.released.store (round + 1, std::memory_order_release);

		while (control.finished.load (std::memory_order_acquire) < producer_count * (round + 1))
		{
			std::this_thread::yield ();
		}
		for (auto const & plan : plans)
		{
			offered += plan.per_round;
		}

		auto const drain_start = steady_clock::now ();
		if (!paced)
		{
			target.set_baseline (drain_start);
			processor.start ();
		}
		while (!drained (offered))
		{
			std::this_thread::yield ();
		}
		result.elapsed += steady_clock::now () - (paced ? begin : drain_start);
		processor.stop ();
	}

	for (auto & producer : producers)
	{
		producer.join ();
	}

	result.offered = total;
	result.dispatched = target.dispatched ();
	result.dropped = dropped_total (stats);
	result.reorders = target.reorders ();
	std::vector<size_t> accepted (channel_count, 0);
	for (auto const & producer : stats)
	{
		result.dropped_hot += producer.dropped_hot.load (std::memory_order_relaxed);
		result.busy += producer.busy;
		for (size_t channel = 0; channel < channel_count; ++channel)
		{
			accepted[channel] += producer.accepted[channel];
		}
	}
	result.offered_hot = per_channel[0];

	auto const & latencies = target.latencies ();
	std::vector<uint32_t> all;
	std::vector<uint32_t> hot;
	std::vector<uint32_t> cold;
	all.reserve (latencies.size ());
	for (uint32_t channel = 0; channel < channel_count; ++channel)
	{
		for (size_t position = 0; position < accepted[channel]; ++position)
		{
			auto const latency = latencies[target.index_of ({ channel, position })];
			all.push_back (latency);
			(channel == 0 ? hot : cold).push_back (latency);
		}
	}
	result.all = summarize (all);
	result.hot = summarize (hot);
	result.cold = summarize (cold);
	return result;
}

double percent (uint64_t part, uint64_t whole)
{
	return whole == 0 ? 0.0 : 100.0 * part / whole;
}

void run_benchmark (benchmark::State & state, workload const & load)
{
	size_t const threads = state.range (0);
	uint64_t dispatched = 0;
	for (auto _ : state)
	{
		auto const result = run_scenario (threads, load);
		state.SetIterationTime (std::chrono::duration<double> (result.elapsed).count ());
		dispatched += result.dispatched;

		state.counters["p50_us"] = result.all.p50_us;
		state.counters["p99_us"] = result.all.p99_us;
		state.counters["max_us"] = result.all.max_us;
		state.counters["drop_pct"] = percent (result.dropped, result.offered);
		state.counters["reorder_pct"] = percent (result.reorders, result.dispatched);
		if (load.rate > 0)
		{
			state.counters["offered_per_s"] = result.offered / std::chrono::duration<double> (result.elapsed).count ();
		}
		else
		{
			state.counters["put_ns"] = static_cast<double> (result.busy.count ()) / result.offered;
		}
		if (load.hot_percent > 0)
		{
			state.counters["hot_p99_us"] = result.hot.p99_us;
			state.counters["cold_p99_us"] = result.cold.p99_us;
			state.counters["hot_drop_pct"] = percent (result.dropped_hot, result.offered_hot);
			state.counters["cold_drop_pct"] = percent (result.dropped - result.dropped_hot, result.offered - result.offered_hot);
		}
	}
	state.SetItemsProcessed (dispatched);
}

void register_family (std::string const & family, workload const & load)
{
	auto * registered = benchmark::RegisterBenchmark ("BM_message_processor/" + family, [load] (benchmark::State & state) {
		run_benchmark (state, load);
	});
	registered->UseManualTime ()->Iterations (1)->Unit (benchmark::kMillisecond)->ArgName ("threads");
	for (auto threads : thread_counts)
	{
		registered->Arg (threads);
	}
}

std::string rate_name (uint64_t rate)
{
	return rate >= 1'000'000 ? std::to_string (rate / 1'000'000) + "m" : std::to_string (rate / 1'000) + "k";
}

void register_all ()
{
	workload const burst{ .rounds = burst_rounds, .messages = channel_count * nano::message_processor_config{}.max_queue };
	register_family ("burst", burst);

	for (auto rate : paced_rates)
	{
		workload const paced{ .messages = static_cast<size_t> (rate * paced_duration / 1s), .rate = rate };
		register_family ("paced_" + rate_name (rate), paced);
	}

	workload const hot{ .messages = static_cast<size_t> (hot_rate * paced_duration / 1s), .rate = hot_rate, .hot_percent = hot_percent };
	register_family ("hot", hot);
}

struct registrar
{
	registrar ()
	{
		register_all ();
	}
};
[[maybe_unused]] registrar const registrar_instance;
}
