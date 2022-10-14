#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>

nano::bandwidth_limiter::bandwidth_limiter (std::size_t limit_a, double burst_ratio_a) :
	bucket (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a)
{
}

bool nano::bandwidth_limiter::should_drop (std::size_t message_size_a)
{
	return !bucket.try_consume (nano::narrow_cast<unsigned int> (message_size_a));
}

void nano::bandwidth_limiter::reset (std::size_t limit_a, double burst_ratio_a)
{
	bucket.reset (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a);
}