#include <nano/lib/rate_limiting.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

using namespace std::chrono_literals;

using test_clock = nano::rate::token_bucket::clock;

/*
 * token_bucket
 */

TEST (token_bucket, construction_starts_full)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_EQ (bucket.capacity (), 1000);
	ASSERT_EQ (bucket.available (now), 1000);
	ASSERT_TRUE (bucket.can_consume (1000, now));
}

TEST (token_bucket, basic_consume)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_TRUE (bucket.consume (400, now));
	ASSERT_TRUE (bucket.consume (400, now));
	// Only 200 tokens left, a large request must fail
	ASSERT_FALSE (bucket.consume (400, now));
	ASSERT_EQ (bucket.available (now), 200);
}

TEST (token_bucket, drained_rejects)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000000, 1.0, now };
	ASSERT_TRUE (bucket.consume (1000000, now));
	ASSERT_FALSE (bucket.consume (500000, now));
	ASSERT_FALSE (bucket.can_consume (500000, now));
}

TEST (token_bucket, consume_zero_always_granted)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 1.0, now };
	ASSERT_TRUE (bucket.consume (10, now));
	// Even with a drained balance, zero tokens must be granted immediately
	ASSERT_TRUE (bucket.consume (0, now));
	ASSERT_TRUE (bucket.can_consume (0, now));
	ASSERT_EQ (bucket.time_to_consume (0, now), 0ms);
}

TEST (token_bucket, unlimited)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 0, 1.0, now };
	ASSERT_TRUE (bucket.consume (std::numeric_limits<std::size_t>::max (), now));
	ASSERT_TRUE (bucket.consume (std::numeric_limits<std::size_t>::max (), now));
	ASSERT_TRUE (bucket.can_consume (std::numeric_limits<std::size_t>::max (), now));
	ASSERT_EQ (bucket.time_to_consume (std::numeric_limits<std::size_t>::max (), now), 0ms);
	ASSERT_EQ (bucket.limit ().rate, 0);
	ASSERT_EQ (bucket.capacity (), 0);
	ASSERT_EQ (bucket.available (now), 0);
}

TEST (token_bucket, burst_capacity)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 3.0, now };
	ASSERT_EQ (bucket.capacity (), 30);
	ASSERT_TRUE (bucket.consume (30, now));
	ASSERT_FALSE (bucket.consume (1, now));
}

TEST (token_bucket, minimum_capacity)
{
	// Tiny burst ratios must still leave a usable bucket of at least one token
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 0.001, now };
	ASSERT_EQ (bucket.capacity (), 1);
	ASSERT_TRUE (bucket.consume (1, now));
	ASSERT_FALSE (bucket.consume (1, now));
}

TEST (token_bucket, refill)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10000, 1.0, now };
	ASSERT_TRUE (bucket.consume (10000, now));
	ASSERT_FALSE (bucket.consume (1, now));
	// At 10k tokens/s, 100ms refills exactly 1000 tokens
	ASSERT_EQ (bucket.available (now + 100ms), 1000);
	ASSERT_TRUE (bucket.consume (1000, now + 100ms));
	ASSERT_FALSE (bucket.consume (1, now + 100ms));
}

TEST (token_bucket, busy_spin)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_TRUE (bucket.consume (1000, now));
	// Hammer a drained bucket with rejected requests every microsecond; each call accrues only 0.001 tokens
	for (int i = 1; i <= 100000; ++i)
	{
		ASSERT_FALSE (bucket.consume (1000, now + i * 1us));
	}
	// The fractional refill must have accumulated to ~100 tokens instead of being truncated away
	ASSERT_TRUE (bucket.consume (100, now + 100ms));
}

TEST (token_bucket, refill_clamped_to_capacity)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 0.01, now }; // Capacity 10
	ASSERT_TRUE (bucket.consume (10, now));
	// 100ms at 1000 tokens/s would be 100 tokens, but the bucket must clamp at its capacity
	ASSERT_EQ (bucket.available (now + 100ms), 10);
	ASSERT_TRUE (bucket.consume (10, now + 100ms));
	ASSERT_FALSE (bucket.consume (1, now + 100ms));
}

TEST (token_bucket, time_to_consume)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_EQ (bucket.time_to_consume (1000, now), 0ms);
	ASSERT_TRUE (bucket.consume (1000, now));
	// Deficit of 100 tokens at 1000 tokens/s is exactly 100ms
	ASSERT_EQ (bucket.time_to_consume (100, now), 100ms);
	ASSERT_EQ (bucket.time_to_consume (1000, now), 1000ms);
	// Partially refilled deficits shrink accordingly
	ASSERT_EQ (bucket.time_to_consume (100, now + 50ms), 50ms);
}

TEST (token_bucket, time_to_consume_is_sufficient)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_TRUE (bucket.consume (1000, now));
	auto const wait = bucket.time_to_consume (100, now);
	// Waiting the returned duration must be enough for the request to succeed
	ASSERT_TRUE (bucket.consume (100, now + wait));
}

TEST (token_bucket, oversized_request_granted_when_full)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 1.0, now }; // Capacity 10
	ASSERT_TRUE (bucket.can_consume (100, now));
	ASSERT_EQ (bucket.time_to_consume (100, now), 0ms);
	ASSERT_TRUE (bucket.consume (100, now));
}

TEST (token_bucket, oversized_request_overdraws)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 1.0, now }; // Capacity 10
	ASSERT_TRUE (bucket.consume (100, now));
	// Balance is now -90, nothing is consumable until the debt refills
	ASSERT_EQ (bucket.available (now), 0);
	ASSERT_FALSE (bucket.consume (1, now));
	ASSERT_FALSE (bucket.can_consume (100, now));
	// Deficit of 91 tokens at 10 tokens/s is exactly 9.1s
	ASSERT_EQ (bucket.time_to_consume (1, now), 9100ms);
	ASSERT_TRUE (bucket.consume (1, now + 9100ms));
}

TEST (token_bucket, oversized_request_rejected_when_not_full)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 1.0, now };
	ASSERT_TRUE (bucket.consume (1, now));
	// Bucket is no longer full, so a request above capacity must wait
	ASSERT_FALSE (bucket.consume (100, now));
	ASSERT_FALSE (bucket.can_consume (100, now));
	// Oversized requests only need a full bucket, deficit of 1 token at 10 tokens/s
	ASSERT_EQ (bucket.time_to_consume (100, now), 100ms);
	ASSERT_TRUE (bucket.consume (100, now + 100ms));
}

TEST (token_bucket, can_consume_does_not_consume)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	for (int i = 0; i < 100; ++i)
	{
		ASSERT_TRUE (bucket.can_consume (1000, now));
	}
	ASSERT_TRUE (bucket.consume (1000, now));
}

TEST (token_bucket, getters)
{
	nano::rate::token_bucket bucket{ 100, 2.5 };
	ASSERT_EQ (bucket.capacity (), 250);
	auto limit = bucket.limit ();
	ASSERT_EQ (limit.rate, 100);
	ASSERT_EQ (limit.burst_ratio, 2.5);
}

TEST (token_bucket, rate_limit_construction)
{
	nano::rate::token_bucket bucket{ nano::rate_limit{ 100, 2.5 } };
	ASSERT_EQ (bucket.limit ().rate, 100);
	ASSERT_EQ (bucket.limit ().burst_ratio, 2.5);
	nano::rate::token_bucket unlimited{ nano::rate_limit::unlimited () };
	ASSERT_TRUE (unlimited.consume (std::numeric_limits<std::size_t>::max ()));
}

TEST (token_bucket, reset_replaces_limits)
{
	nano::rate::token_bucket bucket{ 100, 2.5 };
	bucket.reset (200, 1.5);
	ASSERT_EQ (bucket.limit ().rate, 200);
	ASSERT_EQ (bucket.limit ().burst_ratio, 1.5);
	ASSERT_EQ (bucket.capacity (), 300);
}

TEST (token_bucket, reset_carries_balance)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000, 1.0, now };
	ASSERT_TRUE (bucket.consume (1000, now));
	// A drained bucket must not become full again just because the limits changed
	bucket.reset (1000000, 1.0, now);
	ASSERT_EQ (bucket.available (now), 0);
	ASSERT_FALSE (bucket.consume (1, now));
}

TEST (token_bucket, reset_clamps_to_new_capacity)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 1000000, 1.0, now };
	bucket.reset (10, 1.0, now);
	ASSERT_EQ (bucket.available (now), 10);
	ASSERT_TRUE (bucket.consume (10, now));
	ASSERT_FALSE (bucket.consume (1, now));
}

TEST (token_bucket, reset_from_unlimited_starts_full)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 0, 1.0, now };
	bucket.reset (1000, 1.0, now);
	ASSERT_EQ (bucket.available (now), 1000);
	ASSERT_TRUE (bucket.consume (1000, now));
	ASSERT_FALSE (bucket.consume (1000, now));
}

TEST (token_bucket, reset_to_unlimited)
{
	auto const now = test_clock::now ();
	nano::rate::token_bucket bucket{ 10, 1.0, now };
	ASSERT_TRUE (bucket.consume (10, now));
	bucket.reset (0, 1.0, now);
	ASSERT_TRUE (bucket.consume (std::numeric_limits<std::size_t>::max (), now));
}

TEST (token_bucket, reset_with_rate_limit)
{
	nano::rate::token_bucket bucket{ 100 };
	bucket.reset (nano::rate_limit{ 200, 1.5 });
	ASSERT_EQ (bucket.limit ().rate, 200);
	ASSERT_EQ (bucket.limit ().burst_ratio, 1.5);
}

/*
 * rate_limiter, both threading variants
 */

template <typename Limiter>
class rate_limiter_test : public ::testing::Test
{
};

using rate_limiter_types = ::testing::Types<nano::rate_limiter, nano::rate_limiter_st>;
TYPED_TEST_SUITE (rate_limiter_test, rate_limiter_types);

// Both threading variants must share a single result type
static_assert (std::is_same_v<nano::rate_limiter::result, nano::rate_limiter_st::result>);
static_assert (std::is_same_v<nano::rate_limiter::result, nano::limiter_result>);

TYPED_TEST (rate_limiter_test, consume_granted)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 1000, 1.0, now };
	auto result = limiter.consume (100, now);
	ASSERT_TRUE (result);
	ASSERT_TRUE (result.granted);
	ASSERT_EQ (result.retry_after, 0ms);
}

TYPED_TEST (rate_limiter_test, consume_rejected_with_retry_hint)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 1000, 1.0, now };
	ASSERT_TRUE (limiter.consume (1000, now));
	auto result = limiter.consume (100, now);
	ASSERT_FALSE (result);
	ASSERT_FALSE (result.granted);
	// Deficit of 100 tokens at 1000 tokens/s is exactly 100ms
	ASSERT_EQ (result.retry_after, 100ms);
}

TYPED_TEST (rate_limiter_test, retry_hint_is_sufficient)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 1000, 1.0, now };
	ASSERT_TRUE (limiter.consume (1000, now));
	auto result = limiter.consume (100, now);
	ASSERT_FALSE (result);
	// Waiting the returned duration must be enough for the request to succeed
	ASSERT_TRUE (limiter.try_consume (100, now + result.retry_after));
}

TYPED_TEST (rate_limiter_test, try_consume)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 1000, 1.0, now };
	ASSERT_TRUE (limiter.try_consume (1000, now));
	ASSERT_FALSE (limiter.try_consume (1000, now));
}

TYPED_TEST (rate_limiter_test, can_consume)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 1000, 1.0, now };
	ASSERT_TRUE (limiter.can_consume (1000, now));
	ASSERT_TRUE (limiter.try_consume (1000, now));
	ASSERT_FALSE (limiter.can_consume (1000, now));
}

TYPED_TEST (rate_limiter_test, unlimited)
{
	TypeParam limiter{ 0 };
	ASSERT_TRUE (limiter.consume (std::numeric_limits<std::size_t>::max ()));
	ASSERT_EQ (limiter.consume (std::numeric_limits<std::size_t>::max ()).retry_after, 0ms);
}

TYPED_TEST (rate_limiter_test, reset_and_getters)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 100, 2.0, now };
	ASSERT_EQ (limiter.limit ().rate, 100);
	ASSERT_EQ (limiter.limit ().burst_ratio, 2.0);
	ASSERT_EQ (limiter.capacity (), 200);
	ASSERT_EQ (limiter.available (now), 200);
	limiter.reset (50, 1.0, now);
	ASSERT_EQ (limiter.limit ().rate, 50);
	ASSERT_EQ (limiter.capacity (), 50);
	ASSERT_EQ (limiter.available (now), 50);
}

TYPED_TEST (rate_limiter_test, default_token_count)
{
	auto const now = test_clock::now ();
	TypeParam limiter{ 3, 1.0, now };
	ASSERT_TRUE (limiter.try_consume (1, now));
	ASSERT_TRUE (limiter.try_consume (1, now));
	ASSERT_TRUE (limiter.try_consume (1, now));
	ASSERT_FALSE (limiter.try_consume (1, now));
}

/*
 * rate_limiter, thread safety
 */

TEST (rate_limiter, concurrent_consume_respects_limit)
{
	std::size_t const capacity = 1000;
	nano::rate_limiter limiter{ capacity, 1.0 };

	std::atomic<std::size_t> granted{ 0 };
	auto const started = std::chrono::steady_clock::now ();

	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
	{
		threads.emplace_back ([&] () {
			for (int i = 0; i < 10000; ++i)
			{
				if (limiter.try_consume (1))
				{
					++granted;
				}
			}
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}

	auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - started);

	// All initial capacity must be grantable
	ASSERT_GE (granted, capacity);
	// Grants can never exceed initial capacity plus refill over the elapsed time (1 token/ms), plus rounding slack
	ASSERT_LE (granted, capacity + elapsed.count () + 10);
}

TEST (rate_limiter, concurrent_mixed_operations)
{
	nano::rate_limiter limiter{ 10000, 3.0 };

	// Exercise all operations concurrently; correctness here is the absence of crashes and data races
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
	{
		threads.emplace_back ([&, t] () {
			for (int i = 0; i < 1000; ++i)
			{
				limiter.consume (10);
				limiter.can_consume (10);
				limiter.available ();
				if (t == 0 && i % 100 == 0)
				{
					limiter.reset (10000, 3.0);
				}
			}
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
}
