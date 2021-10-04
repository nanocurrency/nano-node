#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>

namespace nano
{
/* Namespace for shaping (egress) and policing (ingress) rate limiting algorithms */
namespace rate
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
		 * @param max_token_count_a Maximum number of tokens in this bucket, which limits bursts.
		 * @param refill_rate_a Token refill rate, which limits the long term rate (tokens per seconds)
		 */
		token_bucket (size_t max_token_count_a, size_t refill_rate_a);

		/**
		 * Determine if an operation of cost \p tokens_required_a is possible, and deduct from the
		 * bucket if that's the case.
		 * The default cost is 1 token, but resource intensive operations may request
		 * more tokens to be available.
		 */
		bool try_consume (unsigned tokens_required_a = 1);

		/** Returns the largest burst observed */
		size_t largest_burst () const;

		/** Update the max_token_count and/or refill_rate_a parameters */
		void reset (size_t max_token_count_a, size_t refill_rate_a);

	private:
		void refill ();
		size_t max_token_count;
		size_t refill_rate;
		size_t current_size{ 0 };
		/** The minimum observed bucket size, from which the largest burst can be derived */
		size_t smallest_size{ 0 };
		std::chrono::steady_clock::time_point last_refill;
		mutable nano::mutex bucket_mutex;
	};
}
}
