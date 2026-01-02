#pragma once

#include <nano/lib/logging.hpp>
#include <nano/lib/stats_enums.hpp>

#include <cstdint>
#include <string_view>

namespace nano::messages
{
/**
 * Message types are serialized to the network and existing values must thus never change as
 * types are added, removed and reordered in the enum.
 */
enum class message_type : uint8_t
{
	invalid = 0x0,
	not_a_type = 0x1,
	keepalive = 0x2,
	publish = 0x3,
	confirm_req = 0x4,
	confirm_ack = 0x5,
	bulk_pull = 0x6,
	bulk_push = 0x7,
	frontier_req = 0x8,
	/* deleted 0x9 */
	node_id_handshake = 0x0a,
	bulk_pull_account = 0x0b,
	telemetry_req = 0x0c,
	telemetry_ack = 0x0d,
	asc_pull_req = 0x0e,
	asc_pull_ack = 0x0f,
};

std::string_view to_string (message_type);
nano::stat::detail to_stat_detail (message_type);
nano::log::detail to_log_detail (message_type);

enum class bulk_pull_account_flags : uint8_t
{
	pending_hash_and_amount = 0x0,
	pending_address_only = 0x1,
	pending_hash_amount_and_address = 0x2
};
}
