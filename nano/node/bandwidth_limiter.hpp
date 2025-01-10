#pragma once

#include <celerix/lib/rate_limiting.hpp>
#include <celerix/node/fwd.hpp>
#include <celerix/node/transport/traffic_type.hpp>

namespace celerix
{
class bandwidth_limiter_config final
{
public:
	explicit bandwidth_limiter_config (celerix::node_config const &);

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
	explicit bandwidth_limiter (celerix::node_config const &);

	/**
	 * Check whether packet falls withing bandwidth limits and should be allowed
	 * @return true if OK, false if needs to be dropped
	 */
	bool should_pass (std::size_t buffer_size, celerix::transport::traffic_type type);
	/**
	 * Reset limits of selected limiter type to values passed in arguments
	 */
	void reset (std::size_t limit, double burst_ratio, celerix::transport::traffic_type type = celerix::transport::traffic_type::generic);

	celerix::container_info container_info () const;

private:
	/**
	 * Returns reference to limiter corresponding to the limit type
	 */
	celerix::rate_limiter & select_limiter (celerix::transport::traffic_type type);

private:
	bandwidth_limiter_config const config;

private:
	celerix::rate_limiter limiter_generic;
	celerix::rate_limiter limiter_bootstrap;
};
}
