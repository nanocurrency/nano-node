#include <nano/node/fair_queue_traits.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <vector>

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

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::serve };
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

/*
 * Under serve a dead origin keeps its requests until they are drained and only then goes, a channel the test lets go of is not dead
 */
TEST (fair_queue, purge)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::serve };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

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

	// Only closing the channel should make it eligible for the purge
	channel1->close ();
	channel2.reset ();

	// Until the queue is drained, the entries are still present and nothing is dropped
	ASSERT_EQ (queue.maintenance (), 0);
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);

	queue.next_batch (999);

	// Drained, the dead origin goes and still nothing counts as dropped
	ASSERT_EQ (queue.maintenance (), 0);
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

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::serve };
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

/*
 * Under discard a dead origin goes at the next purge with everything still queued under it, and the live origins are untouched
 */
TEST (fair_queue, purge_discard)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel2 });
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 2);

	// Alive, maintenance leaves both origins alone
	ASSERT_EQ (queue.maintenance (), 0);
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 2);

	// Dead, channel 1 goes with both of its requests and the total follows
	channel1->close ();
	ASSERT_EQ (queue.maintenance (), 2);
	ASSERT_EQ (queue.size (), 1);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel1 }), 0);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 1);

	// What is left is channel 2's request
	auto batch = queue.next_batch (999);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch[0].first, 9);
	ASSERT_EQ (batch[0].second.channel, channel2);
	ASSERT_TRUE (queue.empty ());
}

/*
 * A new origin runs maintenance too, so dead origins go as the queue grows even when nothing is ever popped, as with a cap of zero; priority is 0 for an origin the queue no longer knows
 */
TEST (fair_queue, maintenance_on_new_origin)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::serve };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 0; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	// A refused request still leaves its origin behind
	ASSERT_FALSE (queue.push (7, { source_enum::live, channel1 }));
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel1 }), 1);

	// Dead and empty, it goes once the next origin appears
	channel1->close ();
	ASSERT_FALSE (queue.push (8, { source_enum::live, channel2 }));
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel1 }), 0);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel2 }), 1);

	// An origin that dies while nothing new arrives waits, a known origin pushing again is not a new one
	channel2->close ();
	ASSERT_FALSE (queue.push (9, { source_enum::live, channel2 }));
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel2 }), 1);

	// The next new origin purges it
	ASSERT_FALSE (queue.push (10, { source_enum::live, channel3 }));
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel2 }), 0);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel3 }), 1);
	ASSERT_TRUE (queue.empty ());
}

/*
 * Origins are served in the order their queues filled, whatever their source values
 */
TEST (fair_queue, fill_order)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	// Filled in the reverse of the source order
	queue.push (7, { source_enum::unchecked });
	queue.push (8, { source_enum::bootstrap });
	queue.push (9, { source_enum::live });

	// The round goes in fill order, not source order
	ASSERT_EQ (queue.next ().second.source, source_enum::unchecked);
	ASSERT_EQ (queue.next ().second.source, source_enum::bootstrap);
	ASSERT_EQ (queue.next ().second.source, source_enum::live);
	ASSERT_TRUE (queue.empty ());
}

/*
 * An origin drained and filled again joins at the end of the round under way, behind the origins still waiting for their turn
 */
TEST (fair_queue, refill_goes_last)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	queue.push (7, { source_enum::bootstrap });
	queue.push (8, { source_enum::live });
	queue.push (9, { source_enum::live });
	queue.push (10, { source_enum::unchecked });
	queue.push (11, { source_enum::unchecked });

	// Bootstrap drains with its only request and live takes its turn
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.next ().first, 8);

	// Bootstrap fills again and waits behind unchecked, which has not had its turn, but ahead of live, which has
	queue.push (12, { source_enum::bootstrap });
	ASSERT_EQ (queue.next ().first, 10);
	ASSERT_EQ (queue.next ().first, 12);
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_EQ (queue.next ().first, 11);
	ASSERT_TRUE (queue.empty ());
}

/*
 * A maintenance run leaves the queued requests of live origins alone and the round carries on where it was instead of starting over
 */
TEST (fair_queue, maintenance_keeps_round)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	queue.push (7, { source_enum::live });
	queue.push (8, { source_enum::live });
	queue.push (9, { source_enum::bootstrap });
	queue.push (10, { source_enum::bootstrap });

	// Live has had its turn when maintenance runs
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.maintenance (), 0);
	ASSERT_EQ (queue.queues_size (), 2);

	// Bootstrap is next, as it was before maintenance
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 10);
	ASSERT_TRUE (queue.empty ());
}

/*
 * Origins that drain and fill again between batches cannot keep an origin with a backlog from its turn, since a refill waits for the round under way to end
 */
TEST (fair_queue, refill_cannot_starve)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	// Live holds a backlog, three other origins one request each
	for (int n = 0; n < 10; ++n)
	{
		queue.push (n, { source_enum::live });
	}
	queue.push (100, { source_enum::bootstrap });
	queue.push (200, { source_enum::unchecked });
	queue.push (300, { source_enum::local });

	// Every batch of two is followed by a refill of the origins it drained, so there is always fresh work joining the ring
	std::map<source_enum, int> turns;
	for (int batch = 0; batch < 6; ++batch)
	{
		auto popped = queue.next_batch (2);
		ASSERT_EQ (popped.size (), 2);
		for (auto & [value, origin] : popped)
		{
			++turns[origin.source];
			if (origin.source != source_enum::live)
			{
				queue.push (value, origin);
			}
		}
	}

	// Six batches of two are twelve turns, three per origin, the backlog included
	ASSERT_EQ (turns[source_enum::live], 3);
	ASSERT_EQ (turns[source_enum::bootstrap], 3);
	ASSERT_EQ (turns[source_enum::unchecked], 3);
	ASSERT_EQ (turns[source_enum::local], 3);
}

/*
 * Under discard the origin the round is on can die in the middle of its share: it leaves the ring with its requests and the round carries on with the next origin, which takes a full share of its own, instead of starting over
 */
TEST (fair_queue, purge_discard_keeps_round)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 2; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel1 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel2 });
	queue.push (12, { source_enum::live, channel2 });
	queue.push (13, { source_enum::live, channel3 });
	queue.push (14, { source_enum::live, channel3 });
	queue.push (15, { source_enum::live, channel3 });

	// Channel 1 has had its share of two and channel 2 is in the middle of its own
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 10);

	// Channel 2 dies, its two remaining requests go with it
	channel2->close ();
	ASSERT_EQ (queue.maintenance (), 2);
	ASSERT_EQ (queue.size (), 4);
	ASSERT_EQ (queue.queues_size (), 2);

	// Channel 3 is next, as it was before the purge, with a full share of two rather than what channel 2 had left, then the round wraps to channel 1
	ASSERT_EQ (queue.next ().first, 13);
	ASSERT_EQ (queue.next ().first, 14);
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_EQ (queue.next ().first, 15);
	ASSERT_TRUE (queue.empty ());
}
/*
 * Under discard an origin waiting for its turn can die: it leaves the ring with its requests while the origin the round is on keeps the rest of its share
 */
TEST (fair_queue, purge_discard_waiting)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 2; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel1 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel2 });
	queue.push (12, { source_enum::live, channel3 });
	queue.push (13, { source_enum::live, channel3 });

	// Channel 1 is in the middle of its share of two
	ASSERT_EQ (queue.next ().first, 7);

	// Channel 2 dies while it waits, both its requests go with it
	channel2->close ();
	ASSERT_EQ (queue.maintenance (), 2);
	ASSERT_EQ (queue.size (), 4);
	ASSERT_EQ (queue.queues_size (), 2);

	// Channel 1 finishes its share, channel 3 takes the turn channel 2 would have had, then channel 1 again
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 12);
	ASSERT_EQ (queue.next ().first, 13);
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_TRUE (queue.empty ());
}

/*
 * Under discard the origins can die down to an empty ring: the last origin of the ring dies while the round is on it and the round wraps to the first, then the rest die, a drained one and the one the round is on, and a new origin starts a round of its own
 */
TEST (fair_queue, purge_discard_ring_end)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel2 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel3 });
	queue.push (12, { source_enum::live, channel3 });

	// The round has reached channel 3, the last of the ring
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.next ().first, 9);
	ASSERT_EQ (queue.next ().first, 11);

	// Channel 3 dies, the batch's maintenance takes its remaining request with it and the batch wraps to channel 1, which drains
	channel3->close ();
	auto batch = queue.next_batch (/* max count */ 1);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch[0].first, 8);
	ASSERT_EQ (queue.size (), 1);
	ASSERT_EQ (queue.queues_size (), 2);

	// The rest die, drained channel 1 and channel 2, which the round is on with a request left
	channel1->close ();
	channel2->close ();
	ASSERT_EQ (queue.maintenance (), 1);
	ASSERT_TRUE (queue.empty ());
	ASSERT_EQ (queue.queues_size (), 0);

	// Nothing is left to pop
	ASSERT_FALSE (queue.next_ready ([] (auto const &) { return true; }).has_value ());
	ASSERT_TRUE (queue.next_batch (999).empty ());

	// A new origin joins the empty ring and is served
	auto channel4 = nano::test::fake_channel (system.node (0));
	queue.push (13, { source_enum::live, channel4 });
	queue.push (14, { source_enum::live, channel4 });
	ASSERT_EQ (queue.next ().first, 13);
	ASSERT_EQ (queue.next ().first, 14);
	ASSERT_TRUE (queue.empty ());
}
/*
 * A new origin runs maintenance before it joins, so a dead origin the round is on leaves the ring first and the newcomer joins just behind the cursor, served before the round comes back to it
 */
TEST (fair_queue, purge_discard_on_new_origin)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel2 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel3 });
	queue.push (12, { source_enum::live, channel3 });

	// The round is on channel 2
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.next ().first, 9);

	// Channel 2 dies and channel 4 appears, which purges channel 2 with its remaining request before channel 4 joins
	channel2->close ();
	auto channel4 = nano::test::fake_channel (system.node (0));
	ASSERT_TRUE (queue.push (13, { source_enum::live, channel4 }));
	ASSERT_EQ (queue.size (), 4);
	ASSERT_EQ (queue.queues_size (), 3);

	// Channel 3 takes the turn channel 2 left, the round wraps to channel 1, and channel 4, which joined behind the cursor, comes before channel 3 again
	ASSERT_EQ (queue.next ().first, 11);
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 13);
	ASSERT_EQ (queue.next ().first, 12);
	ASSERT_TRUE (queue.empty ());
}

/*
 * The reply servers pop with next_ready_batch under discard, and its maintenance can find the origin the round is on dead: the batch carries on with the next ready origin and leaves the refused ones queued
 */
TEST (fair_queue, purge_discard_ready_batch)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::discard };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel2 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel3 });
	queue.push (12, { source_enum::live, channel3 });

	// Every channel has room until the round is on channel 2
	auto const all = [] (auto const &) { return true; };
	auto first = queue.next_ready (all);
	ASSERT_TRUE (first.has_value ());
	ASSERT_EQ (first->first, 7);
	auto second = queue.next_ready (all);
	ASSERT_TRUE (second.has_value ());
	ASSERT_EQ (second->first, 9);

	// Channel 2 dies and channel 1 has no room, so the batch purges channel 2 with its remaining request and takes what channel 3 holds
	channel2->close ();
	auto const not_channel1 = [&channel1] (auto const & origin) { return origin.channel != channel1; };
	auto batch = queue.next_ready_batch (999, not_channel1);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0].first, 11);
	ASSERT_EQ (batch[1].first, 12);

	// Channel 1's request waits for room
	ASSERT_EQ (queue.size (), 1);
	ASSERT_EQ (queue.queues_size (), 2);
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_TRUE (queue.empty ());
}

/*
 * Under serve a dead origin with requests stays in the ring and keeps its turns until it drains, and the maintenance after that erases it without dropping anything
 */
TEST (fair_queue, purge_serve_keeps_round)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>> queue{ nano::dead_origin_policy::serve };
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };
	queue.maintenance_interval = 0s;

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel1 });
	queue.push (9, { source_enum::live, channel2 });
	queue.push (10, { source_enum::live, channel2 });
	queue.push (11, { source_enum::live, channel3 });

	// The round is on channel 2
	ASSERT_EQ (queue.next ().first, 7);
	ASSERT_EQ (queue.next ().first, 9);

	// Channel 2 dies with a request left, maintenance keeps its queue
	channel2->close ();
	ASSERT_EQ (queue.maintenance (), 0);
	ASSERT_EQ (queue.size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);

	// The round carries on and channel 2's last request is served in its turn
	ASSERT_EQ (queue.next ().first, 11);
	ASSERT_EQ (queue.next ().first, 8);
	ASSERT_EQ (queue.next ().first, 10);
	ASSERT_TRUE (queue.empty ());

	// Drained, channel 2 goes at the next maintenance and the live origins stay
	ASSERT_EQ (queue.maintenance (), 0);
	ASSERT_EQ (queue.queues_size (), 2);
	ASSERT_EQ (queue.priority ({ source_enum::live, channel2 }), 0);
}

/*
 * Random pushes, pops, batches, deaths and maintenance under both policies, checked after every step against a model of what each origin holds
 * Deaths land wherever the round happens to be, so purges take queues out of the ring under the cursor, mid share, ahead of it and as its last one
 * Every request must come out once and in its origin's order, a purged origin's never, a purge must only take dead origins, drained ones under serve, and the queue must agree with the model on every size
 */
TEST (fair_queue, purge_random)
{
	nano::test::system system{ 1 };

	using queue_type = nano::fair_queue<int, source_enum, std::shared_ptr<nano::transport::channel>>;
	using channel_ptr = std::shared_ptr<nano::transport::channel>;

	for (auto const policy : { nano::dead_origin_policy::serve, nano::dead_origin_policy::discard })
	{
		queue_type queue{ policy };
		queue.priority_query = [] (auto const & origin) { return origin.source == source_enum::live ? 1 : 3; };
		queue.max_size_query = [] (auto const &) { return 999; };
		queue.maintenance_interval = 0s;

		std::mt19937 rng{ 7 }; // Fixed, so a failure replays

		// Pushes and deaths pick among the newest channels, dead ones included until newer ones push them out
		size_t const window = 8;
		std::vector<channel_ptr> channels;
		for (size_t n = 0; n < window; ++n)
		{
			channels.push_back (nano::test::fake_channel (system.node (0)));
		}

		// What the queue should hold, per origin it knows
		std::map<queue_type::origin_type, std::deque<int>> model;
		int pushed = 0;
		size_t popped = 0;
		size_t dropped_total = 0;
		size_t served_dead = 0;

		// Erases the origins the queue no longer knows, which must be dead and under serve drained, and counts the requests that went with them
		auto const sync = [&] (size_t & dropped) {
			for (auto it = model.begin (); it != model.end ();)
			{
				if (queue.priority (it->first) != 0)
				{
					++it;
					continue;
				}
				ASSERT_FALSE (it->first.channel->alive ());
				if (policy == nano::dead_origin_policy::serve)
				{
					ASSERT_TRUE (it->second.empty ());
				}
				dropped += it->second.size ();
				it = model.erase (it);
			}
		};

		// Takes a popped request off the model, where it must be its origin's oldest
		auto const take = [&] (queue_type::value_type const & entry) {
			auto existing = model.find (entry.second);
			ASSERT_NE (existing, model.end ());
			ASSERT_FALSE (existing->second.empty ());
			ASSERT_EQ (existing->second.front (), entry.first);
			existing->second.pop_front ();
			++popped;
			served_dead += entry.second.channel->alive () ? 0 : 1;
		};

		// Asserts that every origin still holding requests is one the predicate refuses
		auto const all_refused = [&] (auto const & ready) {
			for (auto const & [origin, requests] : model)
			{
				ASSERT_TRUE (requests.empty () || !ready (origin));
			}
		};

		for (int step = 0; step < 5000; ++step)
		{
			auto const recent = std::span (channels).last (window);
			auto const action = rng () % 100;
			size_t dropped = 0;

			if (action < 45)
			{
				// A request from a recent channel under one of two sources with different shares, its maintenance may purge others
				queue_type::origin_type const origin{ rng () % 2 == 0 ? source_enum::live : source_enum::bootstrap, recent[rng () % window] };
				ASSERT_TRUE (queue.push (pushed, origin));
				ASSERT_NO_FATAL_FAILURE (sync (dropped));
				model[origin].push_back (pushed++);
			}
			else if (action < 60)
			{
				// A single pop, which runs no maintenance
				if (!queue.empty ())
				{
					auto entry = queue.next ();
					ASSERT_NO_FATAL_FAILURE (take (entry));
				}
			}
			else if (action < 86)
			{
				// A third of the recent channels have no room
				std::set<channel_ptr> refused;
				for (auto const & channel : recent)
				{
					if (rng () % 3 == 0)
					{
						refused.insert (channel);
					}
				}
				auto const ready = [&refused] (queue_type::origin_type const & origin) { return !refused.contains (origin.channel); };
				size_t const max_count = 1 + rng () % 6;

				if (action < 70)
				{
					// A single ready pop, nothing only when every origin with requests is refused
					auto entry = queue.next_ready (ready);
					if (entry)
					{
						ASSERT_TRUE (ready (entry->second));
						ASSERT_NO_FATAL_FAILURE (take (*entry));
					}
					else
					{
						ASSERT_NO_FATAL_FAILURE (all_refused (ready));
					}
				}
				else if (action < 78)
				{
					// A batch after its maintenance, short only when the queue ran dry
					auto batch = queue.next_batch (max_count);
					ASSERT_NO_FATAL_FAILURE (sync (dropped));
					for (auto const & entry : batch)
					{
						ASSERT_NO_FATAL_FAILURE (take (entry));
					}
					ASSERT_TRUE (batch.size () == max_count || queue.empty ());
				}
				else
				{
					// A ready batch after its maintenance, short only when every origin with requests left is refused
					auto batch = queue.next_ready_batch (max_count, ready);
					ASSERT_NO_FATAL_FAILURE (sync (dropped));
					for (auto const & entry : batch)
					{
						ASSERT_TRUE (ready (entry.second));
						ASSERT_NO_FATAL_FAILURE (take (entry));
					}
					if (batch.size () < max_count)
					{
						ASSERT_NO_FATAL_FAILURE (all_refused (ready));
					}
				}
			}
			else if (action < 94)
			{
				// A recent live channel dies wherever the round is and a fresh one joins the recent
				std::vector<channel_ptr> alive;
				std::ranges::copy_if (recent, std::back_inserter (alive), [] (auto const & channel) { return channel->alive (); });
				if (!alive.empty ())
				{
					alive[rng () % alive.size ()]->close ();
				}
				channels.push_back (nano::test::fake_channel (system.node (0)));
			}
			else
			{
				// Maintenance on its own reports exactly what the purge took
				auto const reported = queue.maintenance ();
				ASSERT_NO_FATAL_FAILURE (sync (dropped));
				ASSERT_EQ (reported, dropped);
			}
			dropped_total += dropped;

			// The queue agrees with the model on every origin and in total
			size_t expected_size = 0;
			for (auto const & [origin, requests] : model)
			{
				ASSERT_EQ (queue.size (origin), requests.size ());
				expected_size += requests.size ();
			}
			ASSERT_EQ (queue.size (), expected_size);
			ASSERT_EQ (queue.queues_size (), model.size ());
		}

		// Draining the rest accounts for every request pushed, popped once or dropped once
		while (!queue.empty ())
		{
			auto entry = queue.next ();
			ASSERT_NO_FATAL_FAILURE (take (entry));
		}
		ASSERT_EQ (popped + dropped_total, static_cast<size_t> (pushed));

		// The run reached the cases it is about, serve serving dead origins and discard dropping their requests
		if (policy == nano::dead_origin_policy::serve)
		{
			ASSERT_GT (served_dead, 0);
			ASSERT_EQ (dropped_total, 0);
		}
		else
		{
			ASSERT_GT (dropped_total, 0);
		}
	}
}
