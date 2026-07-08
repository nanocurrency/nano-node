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
	nano::rate_limit generic;
	nano::rate_limit bootstrap;
};

/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class bandwidth_limiter final
{
public:
	bandwidth_limiter (nano::node_config const &, nano::node_flags const &);

	// Attempts to consume bandwidth tokens; on failure includes how long until retry is worthwhile
	nano::limiter_result consume (std::size_t buffer_size, nano::transport::traffic_type type);
	// Convenience wrapper when the retry hint is not needed
	bool try_consume (std::size_t buffer_size, nano::transport::traffic_type type);
	// Resets limits of selected limiter type to values passed in arguments
	void reset (nano::rate_limit limit, nano::transport::traffic_type type = nano::transport::traffic_type::generic);

	nano::container_info container_info () const;

	// Returns rate and burst ratio of the selected limiter type
	nano::rate_limit get_limit (nano::transport::traffic_type type = nano::transport::traffic_type::generic) const;

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
