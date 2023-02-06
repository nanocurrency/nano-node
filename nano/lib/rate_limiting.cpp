#include <nano/lib/locks.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/utility.hpp>

#include <limits>

nano::rate::token_bucket::token_bucket (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	reset (max_token_count_a, refill_rate_a);
}

bool nano::rate::token_bucket::try_consume (unsigned tokens_required_a)
{
	debug_assert (tokens_required_a <= 1e9);
	nano::lock_guard<nano::mutex> guard{ mutex };
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

std::size_t nano::rate::token_bucket::largest_burst () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return max_token_count - smallest_size;
}

void nano::rate::token_bucket::reset (std::size_t max_token_count_a, std::size_t refill_rate_a)
{
	nano::lock_guard<nano::mutex> guard{ mutex };

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
