#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>

/*
 * bandwidth_limiter
 */

nano::bandwidth_limiter::bandwidth_limiter (std::size_t limit_a, double burst_ratio_a) :
	bucket (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a)
{
}

bool nano::bandwidth_limiter::should_pass (std::size_t message_size_a)
{
	return bucket.try_consume (nano::narrow_cast<unsigned int> (message_size_a));
}

void nano::bandwidth_limiter::reset (std::size_t limit_a, double burst_ratio_a)
{
	bucket.reset (static_cast<std::size_t> (limit_a * burst_ratio_a), limit_a);
}

/*
 * outbound_bandwidth_limiter
 */

nano::outbound_bandwidth_limiter::outbound_bandwidth_limiter (nano::outbound_bandwidth_limiter::config config_a) :
	config_m{ config_a },
	limiter_standard (config_m.standard_limit, config_m.standard_burst_ratio),
	limiter_bootstrap{ config_m.bootstrap_limit, config_m.bootstrap_burst_ratio }
{
}

nano::bandwidth_limiter & nano::outbound_bandwidth_limiter::select_limiter (nano::bandwidth_limit_type type)
{
	switch (type)
	{
		case bandwidth_limit_type::bootstrap:
			return limiter_bootstrap;
		default:
			return limiter_standard;
	}
	debug_assert (false);
}

bool nano::outbound_bandwidth_limiter::should_pass (std::size_t buffer_size, nano::bandwidth_limit_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.should_pass (buffer_size);
}

void nano::outbound_bandwidth_limiter::reset (std::size_t limit, double burst_ratio, nano::bandwidth_limit_type type)
{
	auto & limiter = select_limiter (type);
	limiter.reset (limit, burst_ratio);
}