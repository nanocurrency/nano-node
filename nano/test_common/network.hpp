#pragma once

#include <nano/node/common.hpp>
#include <nano/test_common/system.hpp>

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

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system, nano::node_flags = nano::node_flags ());

	/** speculatively (it is not guaranteed that the port will remain free) find a free tcp binding port and return it */
	uint16_t speculatively_choose_a_free_tcp_bind_port ();
}
}
