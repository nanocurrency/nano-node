#pragma once

#include <nano/lib/rate_limiting.hpp>
#include <nano/node/transport/traffic_type.hpp>

namespace nano
{
/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class bandwidth_limiter final
{
public: // Config
	struct config
	{
		// standard
		std::size_t standard_limit;
		double standard_burst_ratio;
		// bootstrap
		std::size_t bootstrap_limit;
		double bootstrap_burst_ratio;
	};

public:
	explicit bandwidth_limiter (config);

	/**
	 * Check whether packet falls withing bandwidth limits and should be allowed
	 * @return true if OK, false if needs to be dropped
	 */
	bool should_pass (std::size_t buffer_size, nano::transport::traffic_type type);
	/**
	 * Reset limits of selected limiter type to values passed in arguments
	 */
	void reset (std::size_t limit, double burst_ratio, nano::transport::traffic_type type = nano::transport::traffic_type::generic);

private:
	/**
	 * Returns reference to limiter corresponding to the limit type
	 */
	nano::rate_limiter & select_limiter (nano::transport::traffic_type type);

private:
	const config config_m;

private:
	nano::rate_limiter limiter_generic;
	nano::rate_limiter limiter_bootstrap;
};
}