#include <nano/lib/numbers.hpp>
#include <nano/lib/timestamp.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>

#include <thread>
#include <unordered_set>

TEST (timestamp, now)
{
	nano::timestamp_generator generator;
	auto before (generator.timestamp_from_ms (std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ()));
	auto now (generator.now ());
	auto after (generator.timestamp_from_ms (std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ()));
	ASSERT_LE (before, now);
	ASSERT_LE (now, after);
}

TEST (timestamp, basic)
{
	nano::timestamp_generator generator;
	auto one (generator.timestamp_now ());
	ASSERT_EQ (0, generator.component_count (one));
	ASSERT_NE (0, generator.component_time (one));
	auto two (generator.timestamp_now ());
	ASSERT_NE (one, two);
}

TEST (timestamp, count)
{
	nano::timestamp_generator generator;
	auto one (generator.timestamp_now ());
	auto two (generator.timestamp_now ());
	while (generator.component_time (one) != generator.component_time (two))
	{
		one = two;
		two = generator.timestamp_now ();
	}
	ASSERT_EQ (one + 1, two);
}

TEST (timestamp, parallel)
{
	std::mutex mutex;
	std::unordered_set<uint64_t> timestamps;
	timestamps.reserve (100 * 10000);
	nano::timestamp_generator generator;
	std::vector<std::thread> threads;
	for (auto i (0); i < 100; ++i)
	{
		threads.push_back (std::thread ([&timestamps, &generator, &mutex] () {
			for (auto i (0); i < 1000; ++i)
			{
				auto stamp (generator.timestamp_now ());
				std::lock_guard<std::mutex> lock (mutex);
				auto inserted (timestamps.insert (stamp));
				assert (inserted.second);
			}
		}));
	}
	for (auto & i : threads)
	{
		i.join ();
	}
}
