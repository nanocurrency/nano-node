#include <celerix/lib/utility.hpp>
#include <celerix/node/bandwidth_limiter.hpp>
#include <celerix/node/nodeconfig.hpp>

/*
 * bandwidth_limiter
 */

celerix::bandwidth_limiter::bandwidth_limiter (celerix::node_config const & node_config_a) :
	config{ node_config_a },
	limiter_generic{ config.generic_limit, config.generic_burst_ratio },
	limiter_bootstrap{ config.bootstrap_limit, config.bootstrap_burst_ratio }
{
}

celerix::rate_limiter & celerix::bandwidth_limiter::select_limiter (celerix::transport::traffic_type type)
{
	switch (type)
	{
		case celerix::transport::traffic_type::bootstrap_server:
			return limiter_bootstrap;
		default:
			return limiter_generic;
	}
}

bool celerix::bandwidth_limiter::should_pass (std::size_t buffer_size, celerix::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.should_pass (buffer_size);
}

void celerix::bandwidth_limiter::reset (std::size_t limit, double burst_ratio, celerix::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	limiter.reset (limit, burst_ratio);
}

celerix::container_info celerix::bandwidth_limiter::container_info () const
{
	celerix::container_info info;
	info.put ("generic", limiter_generic.size ());
	info.put ("bootstrap", limiter_bootstrap.size ());
	return info;
}

/*
 * bandwidth_limiter_config
 */

celerix::bandwidth_limiter_config::bandwidth_limiter_config (celerix::node_config const & node_config) :
	generic_limit{ node_config.bandwidth_limit },
	generic_burst_ratio{ node_config.bandwidth_limit_burst_ratio },
	bootstrap_limit{ node_config.bootstrap_bandwidth_limit },
	bootstrap_burst_ratio{ node_config.bootstrap_bandwidth_burst_ratio }
{
}