#include <nano/lib/locks.hpp>
#include <nano/lib/rate_limiting.hpp>

#include <limits>

nano::rate::token_bucket::token_bucket (size_t max_token_count_a, size_t refill_rate_a)
{
	// A max token count of 0 indicates unlimited, which in practice means using the
	// maximum available token count. This way the largest burst can still be computed
	// from smallest_size.
	if (max_token_count_a == 0)
	{
		max_token_count_a = std::numeric_limits<std::size_t>::max ();
	}
	max_token_count = smallest_size = current_size = max_token_count_a;
	refill_rate = refill_rate_a;
	last_refill = std::chrono::system_clock::now ();
}

bool nano::rate::token_bucket::try_consume (int tokens_required_a)
{
	nano::lock_guard<std::mutex> lk (bucket_mutex);
	refill ();
	bool possible = current_size >= tokens_required_a;
	if (possible)
	{
		current_size -= tokens_required_a;
	}

	// Keep track of smallest observed bucket size so burst size can be computed (for tests and stats)
	smallest_size = std::min (smallest_size, current_size);

	return possible;
}

void nano::rate::token_bucket::refill ()
{
	auto now (std::chrono::system_clock::now ());
	double tokens_to_add = std::chrono::duration_cast<std::chrono::nanoseconds> (now - last_refill).count () * refill_rate / 1e9;
	current_size = std::min (current_size + tokens_to_add, static_cast<double> (max_token_count));
	last_refill = std::chrono::system_clock::now ();
}

size_t nano::rate::token_bucket::largest_burst () const
{
	nano::lock_guard<std::mutex> lk (bucket_mutex);
	return max_token_count - smallest_size;
}
