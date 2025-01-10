#pragma once

#include <celerix/node/endpoint.hpp>
#include <celerix/test_common/system.hpp>

namespace celerix
{
class node;

namespace transport
{
	class channel;
	class tcp_channel;
}

namespace test
{
	class system;
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<celerix::transport::tcp_channel> establish_tcp (celerix::test::system &, celerix::node &, celerix::endpoint const &);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<celerix::node> add_outer_node (celerix::test::system & system, celerix::node_config const & config_a, celerix::node_flags = celerix::node_flags ());

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<celerix::node> add_outer_node (celerix::test::system & system, celerix::node_flags = celerix::node_flags ());

	/** speculatively (it is not guaranteed that the port will remain free) find a free tcp binding port and return it */
	uint16_t speculatively_choose_a_free_tcp_bind_port ();
}
}
