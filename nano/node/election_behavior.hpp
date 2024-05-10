#pragma once

#include <nano/lib/stats.hpp>

#include <string_view>

namespace nano
{
enum class election_behavior
{
	manual,
	priority,
	/**
	 * Hinted elections:
	 * - shorter timespan
	 * - limited space inside AEC
	 */
	hinted,
	/**
	 * Optimistic elections:
	 * - shorter timespan
	 * - limited space inside AEC
	 * - more frequent confirmation requests
	 */
	optimistic,
};

std::string_view to_string (election_behavior);
nano::stat::detail to_stat_detail (election_behavior);
}
