#pragma once

// Convenience header that includes all message types

#include <nano/lib/memory.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/messages/bulk_pull.hpp>
#include <nano/messages/bulk_pull_account.hpp>
#include <nano/messages/bulk_push.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/messages/frontier_req.hpp>
#include <nano/messages/fwd.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/message.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/messages/message_type.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/telemetry.hpp>
#include <nano/messages/vote_relay.hpp>
