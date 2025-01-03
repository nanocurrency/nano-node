#pragma once

#include <nano/lib/stats.hpp>

#include <string_view>
#include <vector>

namespace nano::transport
{
enum class traffic_type
{
	generic,
	bootstrap_server,
	bootstrap_requests,
	block_broadcast,
	block_broadcast_initial,
	block_broadcast_rpc,
	confirmation_requests,
	keepalive,
	vote,
	vote_rebroadcast,
	vote_reply,
	rep_crawler,
	telemetry,
	test,
};

std::string_view to_string (traffic_type);
std::vector<traffic_type> all_traffic_types ();
nano::stat::detail to_stat_detail (traffic_type);
}