#pragma once

namespace nano::transport
{
/**
 * Used for message prioritization and bandwidth limits
 */
enum class traffic_type
{
	generic,
	bootstrap, // Ascending bootstrap (asc_pull_ack, asc_pull_req) traffic
};
}