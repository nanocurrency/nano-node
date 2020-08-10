#include <nano/lib/timestamp.hpp>

#include <gtest/gtest.h>

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
