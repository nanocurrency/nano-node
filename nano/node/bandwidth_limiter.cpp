#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/nodeconfig.hpp>

/*
 * bandwidth_limiter
 */

nano::bandwidth_limiter::bandwidth_limiter (nano::node_config const & node_config_a) :
	config{ node_config_a },
	limiter_generic{ config.generic_limit, config.generic_burst_ratio },
	limiter_bootstrap{ config.bootstrap_limit, config.bootstrap_burst_ratio }
{
}

nano::rate_limiter & nano::bandwidth_limiter::select_limiter (nano::transport::traffic_type type)
{
	switch (type)
	{
		case nano::transport::traffic_type::bootstrap_server:
			return limiter_bootstrap;
		default:
			return limiter_generic;
	}
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

nano::container_info nano::bandwidth_limiter::container_info () const
{
	nano::container_info info;
	info.put ("generic", limiter_generic.size ());
	info.put ("bootstrap", limiter_bootstrap.size ());
	return info;
}

/*
 * bandwidth_limiter_config
 */

nano::bandwidth_limiter_config::bandwidth_limiter_config (nano::node_config const & node_config) :
	generic_limit{ node_config.bandwidth_limit },
	generic_burst_ratio{ node_config.bandwidth_limit_burst_ratio },
	bootstrap_limit{ node_config.bootstrap_bandwidth_limit },
	bootstrap_burst_ratio{ node_config.bootstrap_bandwidth_burst_ratio }
{
}