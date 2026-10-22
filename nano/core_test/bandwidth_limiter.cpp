#include <nano/messages/publish.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (bandwidth_limiter, with_burst)
{
	nano::test::system system;
	nano::messages::publish message{ nano::dev::network_params.network, nano::dev::genesis };
	auto message_size = message.to_bytes ()->size ();
	auto message_limit = 2; // must be multiple of the number of channels
	nano::node_config node_config = system.default_config ();
	node_config.bandwidth_limit = message_limit * message_size;
	node_config.bandwidth_limit_burst_ratio = 4.0; // High burst
	auto & node = *system.add_node (node_config);
	nano::transport::inproc::channel channel1{ node, node };
	nano::transport::inproc::channel channel2{ node, node };
	// change the bandwidth settings, no packet will be dropped
	channel1.send (message, nano::transport::traffic_type::test);
	channel2.send (message, nano::transport::traffic_type::test);
	channel1.send (message, nano::transport::traffic_type::test);
	channel2.send (message, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (1s, 0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::out));
}
