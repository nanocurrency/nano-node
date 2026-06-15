#pragma once

#include <nano/lib/rate_limiting.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>

namespace nano
{
class bandwidth_limiter_config final
{
public:
	explicit bandwidth_limiter_config (nano::node_config const &);

public:
	std::size_t generic_limit;
	double generic_burst_ratio;

	std::size_t bootstrap_limit;
	double bootstrap_burst_ratio;
};

/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class bandwidth_limiter final
{
public:
	bandwidth_limiter (nano::node_config const &, nano::node_flags const &);

	/**
	 * Consumes bandwidth tokens when packet falls within bandwidth limits
	 * @return true if OK, false if needs to be dropped
	 */
	bool try_consume (std::size_t buffer_size, nano::transport::traffic_type type);
	/**
	 * Reset limits of selected limiter type to values passed in arguments
	 */
	void reset (std::size_t limit, double burst_ratio, nano::transport::traffic_type type = nano::transport::traffic_type::generic);

	nano::container_info container_info () const;

	std::pair<std::size_t, double> get_limit (nano::transport::traffic_type type = nano::transport::traffic_type::generic) const;

private:
	/**
	 * Returns reference to limiter corresponding to the limit type
	 */
	nano::rate_limiter & select_limiter (nano::transport::traffic_type type);
	nano::rate_limiter const & select_limiter (nano::transport::traffic_type type) const;

private:
	bandwidth_limiter_config const config;

private:
	nano::rate_limiter limiter_generic;
	nano::rate_limiter limiter_bootstrap;
};
}
