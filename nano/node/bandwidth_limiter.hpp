#pragma once

#include <nano/lib/rate_limiting.hpp>
#include <nano/node/transport/traffic_type.hpp>

namespace nano
{
/**
 * Enumeration for different bandwidth limits for different traffic types
 */
enum class bandwidth_limit_type
{
	/** For all message */
	standard,
	/** For bootstrap (asc_pull_ack, asc_pull_req) traffic */
	bootstrap
};

nano::bandwidth_limit_type to_bandwidth_limit_type (nano::transport::traffic_type const &);

/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class rate_limiter final
{
public:
	// initialize with limit 0 = unbounded
	rate_limiter (std::size_t limit, double burst_ratio);

	bool should_pass (std::size_t buffer_size);
	void reset (std::size_t limit, double burst_ratio);

private:
	nano::rate::token_bucket bucket;
};

class outbound_bandwidth_limiter final
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
	explicit outbound_bandwidth_limiter (config);

	/**
	 * Check whether packet falls withing bandwidth limits and should be allowed
	 * @return true if OK, false if needs to be dropped
	 */
	bool should_pass (std::size_t buffer_size, bandwidth_limit_type);
	/**
	 * Reset limits of selected limiter type to values passed in arguments
	 */
	void reset (std::size_t limit, double burst_ratio, bandwidth_limit_type = bandwidth_limit_type::standard);

private:
	/**
	 * Returns reference to limiter corresponding to the limit type
	 */
	nano::rate_limiter & select_limiter (bandwidth_limit_type);

private:
	const config config_m;

private:
	nano::rate_limiter limiter_standard;
	nano::rate_limiter limiter_bootstrap;
};
}