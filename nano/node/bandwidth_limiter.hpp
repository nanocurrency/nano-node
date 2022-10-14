#pragma once

#include <nano/lib/rate_limiting.hpp>

namespace nano
{
/**
 * Class that tracks and manages bandwidth limits for IO operations
 */
class bandwidth_limiter final
{
public:
	// initialize with limit 0 = unbounded
	bandwidth_limiter (std::size_t limit, double burst_ratio);

	bool should_drop (std::size_t buffer_size);
	void reset (std::size_t limit, double burst_ratio);

private:
	nano::rate::token_bucket bucket;
};
}