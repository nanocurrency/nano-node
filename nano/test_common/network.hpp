#pragma once

#include <nano/node/common.hpp>

namespace nano
{
class node;

namespace transport
{
	class channel;
	class channel_tcp;
}

namespace test
{
	class system;
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<nano::transport::channel_tcp> establish_tcp (nano::test::system &, nano::node &, nano::endpoint const &);
}
}
