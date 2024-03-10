#include <nano/node/fair_queue.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

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
	ASSERT_EQ (queue.total_size (), 0);
	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, process_one)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 1; };

	queue.push (7, { source_enum::live });
	ASSERT_EQ (queue.total_size (), 1);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 1);
	ASSERT_EQ (queue.size ({ source_enum::bootstrap }), 0);

	auto [result, origin] = queue.next ();
	ASSERT_EQ (result, 7);
	ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	ASSERT_EQ (origin.channel, nullptr);

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
	ASSERT_EQ (queue.total_size (), 3);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 3);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 9);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
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
	ASSERT_EQ (queue.total_size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);
	ASSERT_EQ (queue.size ({ source_enum::live }), 1);
	ASSERT_EQ (queue.size ({ source_enum::bootstrap }), 1);
	ASSERT_EQ (queue.size ({ source_enum::unchecked }), 1);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::bootstrap);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 9);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::unchecked);
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
	ASSERT_EQ (queue.total_size (), 2);
	ASSERT_EQ (queue.queues_size (), 1);
	ASSERT_EQ (queue.size ({ source_enum::live }), 2);

	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 7);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	}
	{
		auto [result, origin] = queue.next ();
		ASSERT_EQ (result, 8);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, round_robin_with_priority)
{
	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const & origin) {
		switch (std::get<source_enum> (origin.sources))
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
	ASSERT_EQ (queue.total_size (), 9);

	// Processing 1x live, 2x bootstrap, 3x unchecked before moving to the next source
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::live);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::bootstrap);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::bootstrap);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::unchecked);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::unchecked);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::unchecked);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::live);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::bootstrap);
	ASSERT_EQ (std::get<source_enum> (queue.next ().second.sources), source_enum::live);

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, source_channel)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (6, { source_enum::live, channel1 });
	queue.push (7, { source_enum::live, channel2 });
	queue.push (8, { source_enum::live, channel3 });
	queue.push (9, { source_enum::live, channel1 }); // Channel 1 has multiple entries
	ASSERT_EQ (queue.total_size (), 4);
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
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
		ASSERT_EQ (origin.channel, channel1);
	}
	{
		auto [result, origin] = channel1_results[1];
		ASSERT_EQ (result, 9);
		ASSERT_EQ (std::get<source_enum> (origin.sources), source_enum::live);
		ASSERT_EQ (origin.channel, channel1);
	}

	ASSERT_TRUE (queue.empty ());
}

TEST (fair_queue, cleanup)
{
	nano::test::system system{ 1 };

	nano::fair_queue<int, source_enum> queue;
	queue.priority_query = [] (auto const &) { return 1; };
	queue.max_size_query = [] (auto const &) { return 999; };

	auto channel1 = nano::test::fake_channel (system.node (0));
	auto channel2 = nano::test::fake_channel (system.node (0));
	auto channel3 = nano::test::fake_channel (system.node (0));

	queue.push (7, { source_enum::live, channel1 });
	queue.push (8, { source_enum::live, channel2 });
	queue.push (9, { source_enum::live, channel3 });
	ASSERT_EQ (queue.total_size (), 3);
	ASSERT_EQ (queue.queues_size (), 3);

	ASSERT_EQ (queue.size ({ source_enum::live, channel1 }), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 1);
	ASSERT_EQ (queue.size ({ source_enum::live, channel3 }), 1);

	channel1->close ();
	channel2->close ();

	ASSERT_TRUE (queue.periodic_update ());

	// Only channel 3 should remain
	ASSERT_EQ (queue.total_size (), 1);
	ASSERT_EQ (queue.queues_size (), 1);

	ASSERT_EQ (queue.size ({ source_enum::live, channel1 }), 0);
	ASSERT_EQ (queue.size ({ source_enum::live, channel2 }), 0);
	ASSERT_EQ (queue.size ({ source_enum::live, channel3 }), 1);
}
