#pragma once

#include <nano/lib/locks.hpp>

#include <chrono>
#include <cstddef>

namespace nano
{
/** Rate and burst parameters of a limiter; a rate of 0 disables limiting */
struct rate_limit
{
	std::size_t rate{ 0 };
	double burst_ratio{ 1.0 };

	static constexpr rate_limit unlimited ()
	{
		return {};
	}
};

// Result of a consume attempt, contextually convertible to bool (true = granted)
struct limiter_result
{
	bool granted{ false };
	// Time until retry is worthwhile; zero when granted; a hint, not a reservation
	std::chrono::milliseconds retry_after{ 0 };

	explicit operator bool () const
	{
		return granted;
	}
};
}

namespace nano::rate
{
/**
 * Token bucket rate limiting algorithm. Refills lazily from a steady clock, no timer thread.
 * Not thread-safe; use nano::rate_limiter for concurrent use.
 * A rate of 0 disables limiting: every request is granted.
 * Requests larger than the bucket capacity are granted when the bucket is full, overdrawing the balance; this preserves the average rate without deadlocking the caller.
 */
class token_bucket final
{
public:
	using clock = std::chrono::steady_clock;

	// The `now` parameters default to the current time; tests may inject timestamps for deterministic behavior

	// Refills `rate` tokens per second; bucket capacity is rate * burst_ratio
	explicit token_bucket (std::size_t rate, double burst_ratio = 1.0, clock::time_point now = clock::now ());
	explicit token_bucket (nano::rate_limit limit, clock::time_point now = clock::now ());

	// Attempts to consume tokens; returns true when granted
	bool consume (std::size_t tokens = 1, clock::time_point now = clock::now ());
	// Checks whether tokens could be consumed right now, without consuming
	bool can_consume (std::size_t tokens = 1, clock::time_point now = clock::now ()) const;
	// Time until `tokens` become consumable; zero if consumable now
	std::chrono::milliseconds time_to_consume (std::size_t tokens = 1, clock::time_point now = clock::now ()) const;

	// Replaces rate and burst ratio; balance is clamped to the new capacity
	void reset (std::size_t rate, double burst_ratio = 1.0, clock::time_point now = clock::now ());
	void reset (nano::rate_limit limit, clock::time_point now = clock::now ());

	// Current rate and burst ratio
	nano::rate_limit limit () const;
	// Maximum token balance; zero indicates no limiting
	std::size_t capacity () const;
	// Current token balance, including pending refill
	std::size_t available (clock::time_point now = clock::now ()) const;

private:
	// Balance projected to `now`, including pending refill, clamped to capacity
	double projected (clock::time_point now) const;
	// Balance needed to grant the request; oversized requests only need a full bucket
	double required (std::size_t tokens) const;

	nano::rate_limit limit_m;
	double capacity_m; // Derived from limit_m on reset, at least one token so that small rates remain usable
	double balance; // Fractional to stay accurate at low rates, may be negative after an oversized request
	clock::time_point last_refill;
};
}

namespace nano
{
/**
 * Rate limiter over nano::rate::token_bucket, thread safety determined by the mutex type.
 * Use the nano::rate_limiter alias by default; nano::rate_limiter_st avoids locking overhead in single-threaded contexts.
 */
template <typename Mutex>
class rate_limiter_impl final
{
public:
	using clock = nano::rate::token_bucket::clock;

	// Refills `rate` tokens per second; bucket capacity is rate * burst_ratio
	explicit rate_limiter_impl (std::size_t rate, double burst_ratio = 1.0, clock::time_point now = clock::now ()) :
		bucket{ rate, burst_ratio, now }
	{
	}

	explicit rate_limiter_impl (nano::rate_limit limit, clock::time_point now = clock::now ()) :
		bucket{ limit, now }
	{
	}

	// Shared across all specializations
	using result = nano::limiter_result;

	// Attempts to consume tokens; on failure includes how long until retry is worthwhile
	result consume (std::size_t tokens = 1, clock::time_point now = clock::now ())
	{
		nano::lock_guard<Mutex> guard{ mutex };
		if (bucket.consume (tokens, now))
		{
			return { true };
		}
		return { false, bucket.time_to_consume (tokens, now) };
	}

	// Convenience wrapper when the retry hint is not needed
	bool try_consume (std::size_t tokens = 1, clock::time_point now = clock::now ())
	{
		return consume (tokens, now).granted;
	}

	// Checks whether tokens could be consumed right now, without consuming; advisory under concurrency
	bool can_consume (std::size_t tokens = 1, clock::time_point now = clock::now ()) const
	{
		nano::lock_guard<Mutex> guard{ mutex };
		return bucket.can_consume (tokens, now);
	}

	// Replaces rate and burst ratio; balance is clamped to the new capacity
	void reset (std::size_t rate, double burst_ratio = 1.0, clock::time_point now = clock::now ())
	{
		nano::lock_guard<Mutex> guard{ mutex };
		bucket.reset (rate, burst_ratio, now);
	}

	void reset (nano::rate_limit limit, clock::time_point now = clock::now ())
	{
		nano::lock_guard<Mutex> guard{ mutex };
		bucket.reset (limit, now);
	}

	// Current rate and burst ratio
	nano::rate_limit limit () const
	{
		nano::lock_guard<Mutex> guard{ mutex };
		return bucket.limit ();
	}

	std::size_t capacity () const
	{
		nano::lock_guard<Mutex> guard{ mutex };
		return bucket.capacity ();
	}

	std::size_t available (clock::time_point now = clock::now ()) const
	{
		nano::lock_guard<Mutex> guard{ mutex };
		return bucket.available (now);
	}

private:
	mutable Mutex mutex;
	nano::rate::token_bucket bucket;
};

// Thread-safe rate limiter (default)
using rate_limiter = rate_limiter_impl<nano::mutex>;
// Single-threaded rate limiter, no locking overhead
using rate_limiter_st = rate_limiter_impl<nano::null_mutex>;
}
