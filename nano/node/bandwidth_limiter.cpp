#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>

/*
 * outbound_bandwidth_limiter
 */

nano::outbound_bandwidth_limiter::outbound_bandwidth_limiter (nano::outbound_bandwidth_limiter::config config_a) :
	config_m{ config_a },
	limiter_standard (config_m.standard_limit, config_m.standard_burst_ratio),
	limiter_bootstrap{ config_m.bootstrap_limit, config_m.bootstrap_burst_ratio }
{
}

nano::rate_limiter & nano::outbound_bandwidth_limiter::select_limiter (nano::bandwidth_limit_type type)
{
	switch (type)
	{
		case bandwidth_limit_type::bootstrap:
			return limiter_bootstrap;
		case bandwidth_limit_type::standard:
			break;
		default:
			debug_assert (false);
			break;
	}
	return limiter_standard;
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

nano::bandwidth_limit_type nano::to_bandwidth_limit_type (const nano::transport::traffic_type & traffic_type)
{
	switch (traffic_type)
	{
		case nano::transport::traffic_type::generic:
			return nano::bandwidth_limit_type::standard;
			break;
		case nano::transport::traffic_type::bootstrap:
			return nano::bandwidth_limit_type::bootstrap;
			break;
	}
	debug_assert (false);
	return {};
}