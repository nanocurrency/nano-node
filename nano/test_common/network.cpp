#include <nano/node/node.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<nano::transport::channel_tcp> nano::test::establish_tcp (nano::test::system & system, nano::node & node, nano::endpoint const & endpoint)
{
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<nano::transport::channel_tcp> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	node.network.tcp_channels.start_tcp (endpoint);
	auto error = system.poll_until_true (2s, [&result, &node, &endpoint] {
		result = node.network.tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (endpoint));
		return result != nullptr;
	});
	return result;
}

std::shared_ptr<nano::node> nano::test::add_outer_node (nano::test::system & system_a, uint16_t port_a)
{
	auto outer_node = std::make_shared<nano::node> (system_a.io_ctx, port_a, nano::unique_path (), system_a.logging, system_a.work);
	outer_node->start ();
	system_a.nodes.push_back (outer_node);
	return outer_node;
}
