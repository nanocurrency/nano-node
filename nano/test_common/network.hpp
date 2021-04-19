#pragma once

#include <nano/node/common.hpp>

namespace nano
{
class node;
class system;

namespace transport
{
	class channel_tcp;
}

/** Waits until a TCP connection is established and returns the TCP channel on success*/
std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::system &, nano::node &, nano::endpoint const &);

/** Returns a callback to be used for start_tcp to send a keepalive*/
std::function<void (std::shared_ptr<nano::transport::channel> channel_a)> keepalive_tcp_callback (nano::node &);
}
