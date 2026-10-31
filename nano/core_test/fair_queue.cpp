#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>

using namespace std::chrono_literals;

namespace
{
enum class source_enum
{
	unknown = 0,
	live,
	bootstrap,
	bootstrap_legacy,
	unchecked,
	local,
	forced,
};
}

TEST (fair_queue, construction)
{
	nano::fair_queue<source_enum, int> queue;
	ASSERT_EQ (queue.size (), 0);
	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, process_one)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 1; };

	queue.push (7, { source_enum::live });
	ASSERT_EQ (queue.size (), 1);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 1);
	ASSERT_EQ (queue.size ({ source_enum::bootstrap }), 0);

	auto [result, origin] = queue.next ();
	ASSERT_EQ (result, 7);
	ASSERT_EQ (origin.source, source_enum::live);
	ASSERT_EQ (origin.channel, std::monostate{});

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, fifo)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::live });
	queue.push (9, { source_enum::live });
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 3);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (origin.source, source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (origin.source, source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 9);
		ASSERT_EQ (origin.source, source_enum::live);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, process_many)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 1; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::bootstrap });
	queue.push (9, { source_enum::unchecked });
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);
	ASSERT_EQ (queue.size ({ source_enum::live }), 1);
	ASSERT_EQ (queue.size ({ source_enum::bootstrap }), 1);
	ASSERT_EQ (queue.size ({ source_enum::unchecked }), 1);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (origin.source, source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (origin.source, source_enum::bootstrap);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 9);
		ASSERT_EQ (origin.source, source_enum::unchecked);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, max_queue_size)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 2; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::live });
	queue.push (9, { source_enum::live });
	ASSERT_EQ (queue.size (), 2);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 2);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (origin.source, source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (origin.source, source_enum::live);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, round_robin_with_priority)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const & origin) {
		switch (origin.source)
		{
			case source_enum::live:
				return 1;
			case source_enum::bootstrap:
				return 2;
			case source_enum::unchecked:
				return 3;
			default:
				return 0;
		}
	};
	queue.max_size_query = [] (auto const &) { return 999; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::live });
	queue.push (9, { source_enum::live });
	queue.push (10, { source_enum::bootstrap });
	queue.push (11, { source_enum::bootstrap });
	queue.push (12, { source_enum::bootstrap });
	queue.push (13, { source_enum::unchecked });
	queue.push (14, { source_enum::unchecked });
	queue.push (15, { source_enum::unchecked });
	ASSERT_EQ (queue.size (), 9);

	// Processing 1x live, 2x bootstrap, 3x unchecked before moving to the next source
	ASSERT_EQ (queue.next ().second.source, source_enum::live);
	ASSERT_EQ (queue.next ().second.source, source_enum::bootstrap);
	ASSERT_EQ (queue.next ().second.source, source_enum::bootstrap);
	ASSERT_EQ (queue.next ().second.source, source_enum::unchecked);
	ASSERT_EQ (queue.next ().second.source, source_enum::unchecked);
	ASSERT_EQ (queue.next ().second.source, source_enum::unchecked);
	ASSERT_EQ (queue.next ().second.source, source_enum::live);
	ASSERT_EQ (queue.next ().second.source, source_enum::bootstrap);
	ASSERT_EQ (queue.next ().second.source, source_enum::live);

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, source_channel)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (6, { source_enum::live, channel1 });
	queue.push (7, { source_enum::live, channel2 });
	queue.push (8, { source_enum::live, channel3 });
	queue.push (9, { source_enum::live, channel1 }); // Channel 1 has multiple entries
	ASSERT_EQ (queue.size (), 4);
	ASSERT_EQ (queue.queues_size (), 3); // Each <source, channel> pair is a separate queue

	ASSERT_EQ (queue.size ({ source_enum::live, channel1 }), 2);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel3 }), 1);

	auto all = queue.next_batch (999);
	ASSERT_EQ (all.size (), 4);

	auto filtered = [&] (auto const & channel) {
		auto r = all | std::views::filter ([&] (auto const & entry) {
			return entry.second.channel == channel;
		});
		std::vector vec (r.begin (), r.end ());
		return vec;
	};

	auto channel1_results = filtered (channel1);
	ASSERT_EQ (channel1_results.size (), 2);

	{
		auto [result, origin] = channel1_results[0];
		ASSERT_EQ (result, 6);
		ASSERT_EQ (origin.source, source_enum::live);
		ASSERT_EQ (origin.channel, channel1);
	}
	{
		auto [result, origin] = channel1_results[1];
		ASSERT_EQ (result, 9);
		ASSERT_EQ (origin.source, source_enum::live);
		ASSERT_EQ (origin.channel, channel1);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, cleanup)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel2 });
	queue.push (9, { source_enum::live, channel3 });
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);

	ASSERT_EQ (queue.size ({ source_enum::live, channel1 }), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel3 }), 1);

	// Only closing the channel should make it eligible for cleanup
	channel1->close ();
	channel2.reset ();

	ASSERT_TRUE (queue.periodic_update (0s));

	// Until the queue is drained, the entries are still present
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);

	queue.next_batch (999);

	ASSERT_TRUE (queue.periodic_update (0s));

	ASSERT_TRUE (queue.empty ());
	ASSERT_EQ (queue.queues_size (), 2);
}

/*
 * Requests of an origin the predicate refuses stay queued while the other origins are served, and come out in arrival order once it is accepted
 */
TEST (fair_queue, next_ready_skips_refused)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::bootstrap });
	queue.push (9, { source_enum::bootstrap });
	queue.push (10, { source_enum::unchecked });
	ASSERT_EQ (queue.size (), 4);

	// With every origin refused a fresh queue finds nothing, keeps everything and leaves its cursor at the start
	ASSERT_FALSE (queue.next_ready ([] (auto const &) { return false; }).has_value ());
	ASSERT_EQ (queue.size (), 4);

	// Bootstrap refused, the batch holds the live and unchecked requests and the queue keeps the bootstrap ones
	auto const not_bootstrap = [] (auto const & origin) { return origin.source != source_enum::bootstrap; };
	auto batch = queue.next_ready_batch (999, not_bootstrap);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0].first, 7);
	ASSERT_EQ (batch[0].second.source, source_enum::live);
	ASSERT_EQ (batch[1].first, 10);
	ASSERT_EQ (batch[1].second.source, source_enum::unchecked);
	ASSERT_EQ (queue.size (), 2);
	ASSERT_EQ (queue.size ({ source_enum::bootstrap }), 2);

	// Nothing left is ready, so another batch is empty and a single pop finds nothing
	ASSERT_TRUE (queue.next_ready_batch (999, not_bootstrap).empty ());
	ASSERT_FALSE (queue.next_ready (not_bootstrap).has_value ());
	ASSERT_EQ (queue.size (), 2);

	// Accepted again, the bootstrap requests come out in arrival order
	auto const all = [] (auto const &) { return true; };
	auto first = queue.next_ready (all);
	ASSERT_TRUE (first.has_value ());
	ASSERT_EQ (first->first, 8);
	ASSERT_EQ (first->second.source, source_enum::bootstrap);
	auto rest = queue.next_ready_batch (999, all);
	ASSERT_EQ (rest.size (), 1);
	ASSERT_EQ (rest[0].first, 9);

	// An empty queue yields nothing instead of asserting
	ASSERT_TRUE (queue.empty ());
	ASSERT_FALSE (queue.next_ready (all).has_value ());
	ASSERT_TRUE (queue.next_ready_batch (999, all).empty ());
}

/*
 * An origin refused in the middle of its share is left at once, and its remaining requests come out in order once it is accepted again
 */
TEST (fair_queue, next_ready_mid_share)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const & origin) { return origin.source == source_enum::bootstrap ? 2 : 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::live });
	queue.push (10, { source_enum::bootstrap });
	queue.push (11, { source_enum::bootstrap });
	queue.push (12, { source_enum::bootstrap });

	bool bootstrap_ready = true;
	auto const ready = [&] (auto const & origin) { return origin.source != source_enum::bootstrap || bootstrap_ready; };

	// Live has a share of one and bootstrap a share of two
	ASSERT_EQ (queue.next_ready (ready).value ().first, 7);
	ASSERT_EQ (queue.next_ready (ready).value ().first, 10);

	// Bootstrap refused before its share is used up, the next pop moves on to live
	bootstrap_ready = false;
	ASSERT_EQ (queue.next_ready (ready).value ().first, 8);

	// Live drained and bootstrap refused, nothing is ready although two requests are queued
	ASSERT_FALSE (queue.next_ready (ready).has_value ());
	ASSERT_TRUE (queue.next_ready_batch (999, ready).empty ());
	ASSERT_EQ (queue.size (), 2);

	// Accepted again, the bootstrap requests come out in arrival order
	bootstrap_ready = true;
	auto batch = queue.next_ready_batch (999, ready);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0].first, 11);
	ASSERT_EQ (batch[1].first, 12);
	ASSERT_TRUE (queue.empty ());
}

/*
 * The predicate sees the origin's channel, so requests can wait on the state of the channel they belong to
 */
TEST (fair_queue, next_ready_by_channel)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (6, { source_enum::live, channel1 });
	queue.push (7, { source_enum::live, channel2 });
	queue.push (8, { source_enum::live, channel3 });
	queue.push (9, { source_enum::live, channel1 });

	// Channel 2 refused, its request stays while the other channels are served
	auto const not_channel2 = [&] (auto const & origin) { return origin.channel != channel2; };
	auto batch = queue.next_ready_batch (999, not_channel2);
	ASSERT_EQ (batch.size (), 3);
	ASSERT_TRUE (std::ranges::none_of (batch, [&] (auto const & entry) { return entry.second.channel == channel2; }));
	ASSERT_EQ (queue.size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 1);

	// Accepted again, channel 2's request is the only one left
	auto rest = queue.next_ready_batch (999, [] (auto const &) { return true; });
	ASSERT_EQ (rest.size (), 1);
	ASSERT_EQ (rest[0].first, 7);
	ASSERT_EQ (rest[0].second.channel, channel2);
	ASSERT_TRUE (queue.empty ());
}

/*
 * An evicting push into a full origin drops that origin's oldest request and hands it back, so the origin keeps its most recent requests in arrival order; an origin with no room at all still refuses
 */
TEST (fair_queue, push_evicting)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const & origin) { return origin.source == source_enum::forced ? 0 : 2; };

	// With room it behaves like push and evicts nothing
	auto first = queue.push_evicting (7, { source_enum::live });
	ASSERT_TRUE (first.added);
	ASSERT_FALSE (first.evicted.has_value ());
	auto second = queue.push_evicting (8, { source_enum::live });
	ASSERT_TRUE (second.added);
	ASSERT_FALSE (second.evicted.has_value ());
	ASSERT_EQ (queue.size (), 2);

	// The origin is full, so its oldest request makes room and comes back while the size stays at the cap
	auto third = queue.push_evicting (9, { source_enum::live });
	ASSERT_TRUE (third.added);
	ASSERT_EQ (third.evicted.value (), 7);
	ASSERT_EQ (queue.size (), 2);
	ASSERT_EQ (queue.size ({ source_enum::live }), 2);

	// Another origin is untouched by the eviction
	ASSERT_TRUE (queue.push (10, { source_enum::bootstrap }));
	ASSERT_EQ (queue.size (), 3);

	// A cap of zero refuses without evicting anything
	auto refused = queue.push_evicting (11, { source_enum::forced });
	ASSERT_FALSE (refused.added);
	ASSERT_FALSE (refused.evicted.has_value ());
	ASSERT_EQ (queue.size (), 3);

	// The surviving requests come out in arrival order per origin
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 10);
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_TRUE (queue.empty ());
}
