#include <nano/lib/locks.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/utility.hpp>

#include <limits>

/*
 * token_bucket
 */

nano::rate::token_bucket::token_bucket (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	reset (max_token_count_a, refill_rate_a);
}

bool nano::rate::token_bucket::can_consume (unsigned tokens_required_a)
{
	debug_assert (tokens_required_a <= 1e9);
	refill ();
	return current_size >= tokens_required_a || refill_rate == unlimited_rate_sentinel;
}

bool nano::rate::token_bucket::try_consume (unsigned tokens_required_a)
{
	debug_assert (tokens_required_a <= 1e9);
	refill ();
	bool possible = current_size >= tokens_required_a;
	if (possible)
	{
		current_size -= tokens_required_a;
	}
	else if (tokens_required_a == 1e9)
	{
		current_size = 0;
	}

	// Keep track of smallest observed bucket size so burst size can be computed (for tests and stats)
	smallest_size = std::min (smallest_size, current_size);

	return possible || refill_rate == unlimited_rate_sentinel;
}

void nano::rate::token_bucket::consume_checked (unsigned tokens_required_a)
{
	auto const consumed = try_consume (tokens_required_a);
	release_assert (consumed);
}

void nano::rate::token_bucket::refill ()
{
	auto now (std::chrono::steady_clock::now ());
	std::size_t tokens_to_add = static_cast<std::size_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (now - last_refill).count () / 1e9 * refill_rate);
	// Only update if there are any tokens to add
	if (tokens_to_add > 0)
	{
		current_size = std::min (current_size + tokens_to_add, max_token_count);
		last_refill = std::chrono::steady_clock::now ();
	}
}

void nano::rate::token_bucket::reset (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	// A token count of 0 indicates unlimited capacity. We use 1e9 as
	// a sentinel, allowing largest burst to still be computed.
	if (max_token_count_a == 0 || refill_rate_a == 0)
	{
		refill_rate_a = max_token_count_a = unlimited_rate_sentinel;
	}
	max_token_count = smallest_size = current_size = max_token_count_a;
	refill_rate = refill_rate_a;
	last_refill = std::chrono::steady_clock::now ();
}

std::size_t nano::rate::token_bucket::largest_burst () const
{
	return max_token_count - smallest_size;
}

std::size_t nano::rate::token_bucket::size () const
{
	return current_size;
}

/*
 * rate_limiter
 */

nano::rate_limiter::rate_limiter (std::size_t limit_a, double burst_ratio_a) :
	bucket (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a),
	limit{ limit_a },
	burst_ratio{ burst_ratio_a }
{
}

bool nano::rate_limiter::can_consume (std::size_t token_count_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return bucket.can_consume (nano::narrow_cast<unsigned int> (token_count_a));
}

bool nano::rate_limiter::try_consume (std::size_t token_count_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return bucket.try_consume (nano::narrow_cast<unsigned int> (token_count_a));
}

void nano::rate_limiter::consume_checked (std::size_t token_count_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	bucket.consume_checked (nano::narrow_cast<unsigned int> (token_count_a));
}

void nano::rate_limiter::reset (std::size_t limit_a, double burst_ratio_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	bucket.reset (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a);
	limit = limit_a;
	burst_ratio = burst_ratio_a;
}

std::size_t nano::rate_limiter::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return bucket.size ();
}

std::pair<std::size_t, double> nano::rate_limiter::get_limit () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return { limit, burst_ratio };
}
