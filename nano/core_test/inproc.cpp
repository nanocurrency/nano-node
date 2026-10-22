#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <atomic>

using namespace std::chrono_literals;

/*
 * Repeated publishes and votes all reach the destination, since an inproc channel has no duplicate filter.
 * Only the destination counts them as received.
 */
TEST (inproc, delivers_repeats)
{
	nano::test::system system{ 1 };
	auto & node0 = *system.nodes[0];
	// A node in a separate system does not connect to node0 over TCP, so the inproc channel is their only link
	nano::test::system system1{ 1 };
	auto & node1 = *system1.nodes[0];
	nano::transport::inproc::channel channel{ node0, node1 };

	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (nano::dev::genesis->hash ())
				 .destination (nano::dev::genesis_key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	nano::messages::publish const publish{ nano::dev::network_params.network, block };
	channel.send (publish, nano::transport::traffic_type::test);
	channel.send (publish, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in), 2);

	nano::messages::confirm_ack const confirm_ack{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { block }) };
	channel.send (confirm_ack, nano::transport::traffic_type::test);
	channel.send (confirm_ack, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node1.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in), 2);

	ASSERT_EQ (0, node0.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node0.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
}

/*
 * A message whose content fails validation, here a block without enough work, is dropped
 */
TEST (inproc, drops_insufficient_work)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	nano::transport::inproc::channel channel{ node, node };

	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	ASSERT_TRUE (nano::dev::network_params.network.work.validate_entry (*block)); // Work zero is not enough for this block
	channel.send (nano::messages::publish{ nano::dev::network_params.network, block }, nano::transport::traffic_type::test);

	// A keepalive sent right after marks the point by which the publish would have arrived
	channel.send (nano::messages::keepalive{ nano::dev::network_params.network }, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in), 1);
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
}

/*
 * The send callback reports success with the size of the serialized message
 */
TEST (inproc, send_callback)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	nano::transport::inproc::channel channel{ node, node };

	nano::messages::keepalive const keepalive{ nano::dev::network_params.network };
	std::atomic<bool> called{ false };
	boost::system::error_code result;
	std::size_t size{ 0 };
	channel.send (keepalive, nano::transport::traffic_type::test, [&] (boost::system::error_code const & ec, std::size_t size_a) {
		result = ec;
		size = size_a;
		called = true;
	});
	ASSERT_TIMELY (5s, called);
	ASSERT_FALSE (result);
	ASSERT_EQ (size, keepalive.to_bytes ()->size ());
}

/*
 * A channel between two nodes reports the destination as its peer, not the node it sends from
 */
TEST (inproc, describes_destination)
{
	nano::test::system system{ 2 };
	auto & node0 = *system.nodes[0];
	auto & node1 = *system.nodes[1];
	nano::transport::inproc::channel channel{ node0, node1 };

	ASSERT_EQ (node1.get_node_id (), channel.get_node_id ());
	ASSERT_NE (node0.get_node_id (), channel.get_node_id ());
}

TEST (inproc, loopback_channel)
{
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	nano::transport::inproc::channel channel1 (node1, node1);
	ASSERT_EQ (channel1.get_type (), nano::transport::transport_type::loopback);
	ASSERT_EQ (channel1.get_remote_endpoint (), node1.network.endpoint ());
	ASSERT_EQ (channel1.get_network_version (), node1.network_params.network.protocol_version);
	ASSERT_EQ (channel1.get_node_id (), node1.get_node_id ());
	ASSERT_EQ (channel1.get_node_id_optional ().value_or (0), node1.get_node_id ());
	nano::transport::inproc::channel channel2 (node2, node2);
	++node1.network.port;
	ASSERT_NE (channel1.get_remote_endpoint (), node1.network.endpoint ());
}
