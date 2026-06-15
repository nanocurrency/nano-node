#pragma once

#include <nano/lib/locks.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>

/* Namespace for shaping (egress) and policing (ingress) rate limiting algorithms */
namespace nano::rate
{
/**
 * Token bucket based rate limiting. This is suitable for rate limiting ipc/api calls
 * and network traffic, while allowing short bursts.
 *
 * Tokens are refilled at N tokens per second and there's a bucket capacity to limit
 * bursts.
 *
 * A bucket has low overhead and can be instantiated for various purposes, such as one
 * bucket per session, or one for bandwidth limiting. A token can represent bytes,
 * messages, or the cost of API invocations.
 */
class token_bucket
{
public:
	/**
	 * Set up a token bucket.
	 * @param max_token_count Maximum number of tokens in this bucket, which limits bursts.
	 * @param refill_rate Token refill rate, which limits the long term rate (tokens per seconds)
	 */
	token_bucket (std::size_t max_token_count, std::size_t refill_rate);

	/** Returns true when the requested cost is currently available without deducting it. */
	bool can_consume (unsigned tokens_required = 1);
	/**
	 * Deducts the requested cost if currently available.
	 * The default cost is 1 token, but resource intensive operations may request more tokens.
	 */
	bool try_consume (unsigned tokens_required = 1);
	/** Deducts the requested cost and asserts if the caller has not already ensured availability. */
	void consume_checked (unsigned tokens_required = 1);

	/** Update the max_token_count and/or refill_rate_a parameters */
	void reset (std::size_t max_token_count, std::size_t refill_rate);

	/** Returns the largest burst observed */
	std::size_t largest_burst () const;
	std::size_t size () const;

private:
	void refill ();

private:
	std::size_t max_token_count;
	std::size_t refill_rate;

	std::size_t current_size{ 0 };
	/** The minimum observed bucket size, from which the largest burst can be derived */
	std::size_t smallest_size{ 0 };
	std::chrono::steady_clock::time_point last_refill;

	static std::size_t constexpr unlimited_rate_sentinel{ static_cast<std::size_t> (1e9) };
};
}

namespace nano
{
class rate_limiter final
{
public:
	// initialize with limit 0 = unbounded
	rate_limiter (std::size_t limit, double burst_ratio = 1.0);

	// Returns true when the requested cost is currently available without deducting it
	bool can_consume (std::size_t token_count);
	// Deducts the requested cost if currently available
	bool try_consume (std::size_t token_count);
	// Deducts the requested cost and asserts if it is not available
	void consume_checked (std::size_t token_count);
	void reset (std::size_t limit, double burst_ratio = 1.0);

	std::size_t size () const;
	std::pair<std::size_t, double> get_limit () const;

private:
	nano::rate::token_bucket bucket;
	std::size_t limit;
	double burst_ratio;
	mutable nano::mutex mutex;
};
}
