#pragma once

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

	/**
	 * Determine if an operation of cost \p tokens_required is possible, and deduct from the
	 * bucket if that's the case.
	 * The default cost is 1 token, but resource intensive operations may request
	 * more tokens to be available.
	 */
	bool try_consume (unsigned tokens_required = 1);

	/** Returns the largest burst observed */
	std::size_t largest_burst () const;

	/** Update the max_token_count and/or refill_rate_a parameters */
	void reset (std::size_t max_token_count, std::size_t refill_rate);

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

	bool should_pass (std::size_t buffer_size);
	void reset (std::size_t limit, double burst_ratio = 1.0);

private:
	nano::rate::token_bucket bucket;
	mutable nano::mutex mutex;
};
}