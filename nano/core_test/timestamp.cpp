#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timestamp.hpp>

#include <gtest/gtest.h>

#include <boost/format.hpp>

#include <thread>
#include <unordered_set>

TEST (timestamp, now)
{
	nano::timestamp_generator generator;
	ASSERT_FALSE (nano::timestamp_generator::is_steady);
	auto before_ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
	auto before = generator.timestamp_from_ms (before_ms);
	ASSERT_EQ (before_ms, generator.ms_from_timestamp (before));
	auto now = generator.now ();
	auto after_ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
	auto after (generator.timestamp_from_ms (after_ms));
	ASSERT_EQ (after_ms, generator.ms_from_timestamp (after));
	ASSERT_LE (before, now);
	ASSERT_LE (now, after);
}

TEST (timestamp, basic)
{
	nano::timestamp_generator generator;
	auto one = generator.now ();
	ASSERT_NE (0, generator.mask_time (one));
	auto two = generator.now ();
	ASSERT_NE (0, generator.mask_time (two));
	ASSERT_LT (one, two);
}

TEST (timestamp, count)
{
	nano::timestamp_generator generator;
	auto one = generator.now ();
	auto two = generator.now ();
	while (generator.mask_time (one) != generator.mask_time (two))
	{
		one = two;
		two = generator.now ();
	}
	ASSERT_EQ (one + 1, two);
}

TEST (timestamp, parallel)
{
	auto constexpr thread_count = 100;
	auto iteration_count = 1000;
	std::mutex mutex;
	std::unordered_set<uint64_t> timestamps;
	timestamps.reserve (thread_count * iteration_count);
	nano::timestamp_generator generator;
	std::vector<std::thread> threads;
	for (auto i = 0; i < thread_count; ++i)
	{
		threads.emplace_back ([&timestamps, &generator, &mutex, &iteration_count]() {
			for (auto i = 0; i < iteration_count; ++i)
			{
				auto stamp = generator.now ();
				nano::lock_guard<std::mutex> lock (mutex);
				auto inserted = timestamps.insert (stamp);
				ASSERT_TRUE (inserted.second);
			}
		});
	}
	for (auto & i : threads)
	{
		i.join ();
	}
}
