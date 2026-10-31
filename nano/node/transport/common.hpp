#pragma once

#include <nano/lib/function.hpp>
#include <nano/lib/network_types.hpp>
#include <nano/node/transport/fwd.hpp>

#include <boost/container/small_vector.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <system_error>

namespace nano::transport
{
// Pause between two checks whether channels have room again, taken by a sender that found none
// Writes complete within milliseconds, so a pause this short notices freed room promptly without busy polling
constexpr std::chrono::milliseconds capacity_wait{ 25 };

// Channels picked for one send, with inline room for the fanout of a flood or the principal representatives
using channel_list = boost::container::small_vector<std::shared_ptr<channel>, 128>;

/** Completion callback for an outbound connection attempt. An empty error code indicates success. */
using connect_callback = std::function<void (nano::tcp_endpoint const & endpoint, std::error_code ec)>;

/** Policy to affect at which stage a buffer can be dropped */
enum class buffer_drop_policy
{
	/** Can be dropped by bandwidth limiter (default) */
	limiter,
	/** Should not be dropped by bandwidth limiter */
	no_limiter_drop,
	/** Should not be dropped by bandwidth limiter or socket write queue limiter */
	no_socket_drop
};

enum class socket_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};

std::string_view to_string (socket_type);

enum class socket_endpoint
{
	server, // Socket was created by accepting an incoming connection
	client, // Socket was created by initiating an outgoing connection
};

std::string_view to_string (socket_endpoint);
}
