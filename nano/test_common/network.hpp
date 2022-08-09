#pragma once

#include <nano/node/common.hpp>

namespace nano
{
class node;
class system;

namespace transport
{
	class channel;
	class channel_tcp;
}

namespace test
{
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::system &, nano::node &, nano::endpoint const &);
}
}
