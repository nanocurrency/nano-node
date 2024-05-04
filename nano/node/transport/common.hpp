#pragma once

#include <string_view>

namespace nano::transport
{
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