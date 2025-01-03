#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/utility.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <future>

using namespace std::chrono_literals;

TEST (rate, basic)
{
	nano::rate::token_bucket bucket (10, 10);

	// Initial burst
	ASSERT_TRUE (bucket.try_consume (10));
	ASSERT_FALSE (bucket.try_consume (10));

	// With a fill rate of 10 tokens/sec, await 1/3 sec and get 3 tokens
	std::this_thread::sleep_for (300ms);
	ASSERT_TRUE (bucket.try_consume (3));
	ASSERT_FALSE (bucket.try_consume (10));

	// Allow time for the bucket to completely refill and do a full burst
	std::this_thread::sleep_for (1s);
	ASSERT_TRUE (bucket.try_consume (10));
	ASSERT_EQ (bucket.largest_burst (), 10);
}

TEST (rate, network)
{
	// For the purpose of the test, one token represents 1MB instead of one byte.
	// Allow for 10 mb/s bursts (max bucket size), 5 mb/s long term rate
	nano::rate::token_bucket bucket (10, 5);

	// Initial burst of 10 mb/s over two calls
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 5);
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 10);
	ASSERT_FALSE (bucket.try_consume (5));

	// After 200 ms, the 5 mb/s fillrate means we have 1 mb available
	std::this_thread::sleep_for (200ms);
	ASSERT_TRUE (bucket.try_consume (1));
	ASSERT_FALSE (bucket.try_consume (1));
}

TEST (rate, reset)
{
	nano::rate::token_bucket bucket (0, 0);

	// consume lots of tokens, buckets should be unlimited
	ASSERT_TRUE (bucket.try_consume (1000000));
	ASSERT_TRUE (bucket.try_consume (1000000));

	// set bucket to be limited
	bucket.reset (1000, 1000);
	ASSERT_FALSE (bucket.try_consume (1001));
	ASSERT_TRUE (bucket.try_consume (1000));
	ASSERT_FALSE (bucket.try_consume (1000));
	std::this_thread::sleep_for (2ms);
	ASSERT_TRUE (bucket.try_consume (2));

	// reduce the limit
	bucket.reset (100, 100 * 1000);
	ASSERT_FALSE (bucket.try_consume (101));
	ASSERT_TRUE (bucket.try_consume (100));
	std::this_thread::sleep_for (1ms);
	ASSERT_TRUE (bucket.try_consume (100));

	// increase the limit
	bucket.reset (2000, 1);
	ASSERT_FALSE (bucket.try_consume (2001));
	ASSERT_TRUE (bucket.try_consume (2000));

	// back to unlimited
	bucket.reset (0, 0);
	ASSERT_TRUE (bucket.try_consume (1000000));
	ASSERT_TRUE (bucket.try_consume (1000000));
}

TEST (rate, unlimited)
{
	nano::rate::token_bucket bucket (0, 0);
	ASSERT_TRUE (bucket.try_consume (5));
	ASSERT_EQ (bucket.largest_burst (), 5);
	ASSERT_TRUE (bucket.try_consume (static_cast<size_t> (1e9)));
	ASSERT_EQ (bucket.largest_burst (), static_cast<size_t> (1e9));

	// With unlimited tokens, consuming always succeed
	ASSERT_TRUE (bucket.try_consume (static_cast<size_t> (1e9)));
	ASSERT_EQ (bucket.largest_burst (), static_cast<size_t> (1e9));
}

TEST (rate, busy_spin)
{
	// Bucket should refill at a rate of 1 token per second
	nano::rate::token_bucket bucket (1, 1);

	// Run a very tight loop for 5 seconds + a bit of wiggle room
	int counter = 0;
	for (auto start = std::chrono::steady_clock::now (), now = start; now < start + 5500ms; now = std::chrono::steady_clock::now ())
	{
		if (bucket.try_consume ())
		{
			++counter;
		}
	}

	// Bucket starts fully refilled, therefore we see 1 additional request
	ASSERT_EQ (counter, 6);
}