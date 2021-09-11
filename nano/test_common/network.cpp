#include <nano/node/node.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

using namespace std::chrono_literals;

std::shared_ptr<nano::transport::channel_tcp> nano::establish_tcp (nano::system & system, nano::node & node, nano::endpoint const & endpoint)
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

std::function<void (std::shared_ptr<nano::transport::channel> channel_a)> nano::keepalive_tcp_callback (nano::node & node_a)
{
	return [node_w = std::weak_ptr<nano::node> (node_a.shared ())] (std::shared_ptr<nano::transport::channel> channel_a) {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.send_keepalive (channel_a);
		};
	};
}
