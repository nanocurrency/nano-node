#include <nano/lib/utility.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/nodeconfig.hpp>

/*
 * bandwidth_limiter
 */

nano::bandwidth_limiter::bandwidth_limiter (nano::node_config const & node_config, nano::node_flags const & flags) :
	config{ node_config },
	limiter_generic{ flags.super_rebroadcaster ? nano::rate_limit::unlimited () : config.generic },
	limiter_bootstrap{ config.bootstrap }
{
}

nano::rate_limiter & nano::bandwidth_limiter::select_limiter (nano::transport::traffic_type type)
{
	switch (nano::to_bandwidth_limit_type (type))
	{
		case nano::bandwidth_limit_type::generic:
			return limiter_generic;
		case nano::bandwidth_limit_type::bootstrap:
			return limiter_bootstrap;
	}

	debug_assert (false, "unhandled bandwidth limit type");
	return limiter_generic;
}

nano::rate_limiter const & nano::bandwidth_limiter::select_limiter (nano::transport::traffic_type type) const
{
	switch (nano::to_bandwidth_limit_type (type))
	{
		case nano::bandwidth_limit_type::generic:
			return limiter_generic;
		case nano::bandwidth_limit_type::bootstrap:
			return limiter_bootstrap;
	}

	debug_assert (false, "unhandled bandwidth limit type");
	return limiter_generic;
}

nano::limiter_result nano::bandwidth_limiter::consume (std::size_t buffer_size, nano::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.consume (buffer_size);
}

bool nano::bandwidth_limiter::try_consume (std::size_t buffer_size, nano::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	return limiter.try_consume (buffer_size);
}

void nano::bandwidth_limiter::reset (nano::rate_limit limit, nano::transport::traffic_type type)
{
	auto & limiter = select_limiter (type);
	limiter.reset (limit);
}

nano::container_info nano::bandwidth_limiter::container_info () const
{
	nano::container_info info;
	info.put ("generic", limiter_generic.available ());
	info.put ("bootstrap", limiter_bootstrap.available ());
	return info;
}

nano::rate_limit nano::bandwidth_limiter::get_limit (nano::transport::traffic_type type) const
{
	return select_limiter (type).limit ();
}

/*
 * bandwidth_limit_type
 */

nano::bandwidth_limit_type nano::to_bandwidth_limit_type (nano::transport::traffic_type type)
{
	switch (type)
	{
		case nano::transport::traffic_type::bootstrap_server:
			return nano::bandwidth_limit_type::bootstrap;
		default:
			return nano::bandwidth_limit_type::generic;
	}
}

/*
 * bandwidth_limiter_config
 */

nano::bandwidth_limiter_config::bandwidth_limiter_config (nano::node_config const & node_config) :
	generic{ node_config.bandwidth_limit, node_config.bandwidth_limit_burst_ratio },
	bootstrap{ node_config.bootstrap_bandwidth_limit, node_config.bootstrap_bandwidth_burst_ratio }
{
}
