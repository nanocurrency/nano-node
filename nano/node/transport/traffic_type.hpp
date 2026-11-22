#pragma once

#include <nano/lib/stats.hpp>

#include <string_view>
#include <vector>

namespace nano::transport
{
enum class traffic_type
{
	generic,
	bootstrap,
	bootstrap_server,
	block_broadcast,
	block_broadcast_initial,
	block_broadcast_rpc,
	block_rebroadcast,
	confirmation_requests,
	keepalive,
	vote,
	vote_normal,
	vote_final,
	vote_rebroadcast,
	vote_reply,
	rep_crawler,
	telemetry,
	test,
};

std::string_view to_string (traffic_type);
nano::stat::detail to_stat_detail (traffic_type);

std::vector<traffic_type> const & all_traffic_types ();

// Messages of the type a channel sends per turn before it serves the next type
size_t traffic_priority (traffic_type);
}