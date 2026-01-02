#pragma once

#include <nano/lib/fwd.hpp>

namespace nano::messages
{
enum class message_type : uint8_t;
enum class bulk_pull_account_flags : uint8_t;
enum class asc_pull_type : uint8_t;
enum class telemetry_maker : uint8_t;
enum class deserialize_message_status;

class message;
class message_header;
class message_visitor;

class keepalive;
class publish;
class confirm_req;
class confirm_ack;
class frontier_req;
class telemetry_data;
class telemetry_req;
class telemetry_ack;
class bulk_pull;
class bulk_pull_account;
class bulk_push;
class node_id_handshake;
class asc_pull_req;
class asc_pull_ack;
}

namespace nano
{
using block_uniquer = nano::uniquer<nano::uint256_union, nano::block>;
using vote_uniquer = nano::uniquer<nano::block_hash, nano::vote>;
}
