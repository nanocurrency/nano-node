#pragma once

namespace nano::transport
{
/**
 * Used for message prioritization and bandwidth limits
 */
enum class traffic_type
{
	generic,
	/** For bootstrap (asc_pull_ack, asc_pull_req) traffic */
	bootstrap
};
}