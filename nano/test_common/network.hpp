#pragma once

#include <nano/node/endpoint.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/traffic_type.hpp>
#include <nano/test_common/system.hpp>

namespace nano
{
namespace test
{
	class system;
	/** Waits until a TCP connection is established and returns the TCP channel on success*/
	std::shared_ptr<nano::transport::tcp_channel> establish_tcp (nano::test::system &, nano::node &, nano::endpoint const &);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system, nano::node_config const & config_a, nano::node_flags const &);
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system, nano::node_config const & config_a);

	/** Adds a node to the system without establishing connections */
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system, nano::node_flags const &);
	std::shared_ptr<nano::node> add_outer_node (nano::test::system & system);

	/** speculatively (it is not guaranteed that the port will remain free) find a free tcp binding port and return it */
	uint16_t speculatively_choose_a_free_tcp_bind_port ();

	/** Congests the node's network for the traffic type: every channel it has now reports that traffic as full until unsaturate_network () */
	void saturate_network (nano::node &, nano::transport::traffic_type);
	void unsaturate_network (nano::node &, nano::transport::traffic_type);
}
}
