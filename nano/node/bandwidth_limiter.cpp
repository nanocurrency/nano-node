#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>

/*
 * bandwidth_limiter
 */

nano::bandwidth_limiter::bandwidth_limiter (nano::bandwidth_limiter::config config_a) :
	config_m{ config_a },
	limiter_generic{ config_m.standard_limit, config_m.standard_burst_ratio },
	limiter_bootstrap{ config_m.bootstrap_limit, config_m.bootstrap_burst_ratio }
{
}

nano::rate_limiter & nano::bandwidth_limiter::select_limiter (nano::transport::traffic_type type)
{
	switch (type)
	{
		case nano::transport::traffic_type::bootstrap:
			return limiter_bootstrap;
		case nano::transport::traffic_type::generic:
			return limiter_generic;
			break;
		default:
			debug_assert (false, "missing traffic type");
			break;
	}
	return limiter_generic;
}

bool nano::bandwidth_limiter::should_pass (std::size_t buffer_size, nano::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.should_pass (buffer_size);
}

void nano::bandwidth_limiter::reset (std::size_t limit, double burst_ratio, nano::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	limiter.reset (limit, burst_ratio);
}