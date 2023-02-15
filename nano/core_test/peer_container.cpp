#include <nano/node/socket.hpp>
#include <nano/node/transport/tcp.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <memory>

using namespace std::chrono_literals;

TEST (peer_container, empty_peers)
{
	nano::test::system system (1);
	nano::network & network (system.nodes[0]->network);
	system.nodes[0]->network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, network.size ());
}

// Test a node cannot connect to its own endpoint.
TEST (peer_container, no_self_incoming)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	node.network.tcp_channels.start_tcp (node.network.endpoint ());
	auto error = system.poll_until_true (2s, [&node] {
		auto result = node.network.tcp_channels.find_channel (nano::transport::map_endpoint_to_tcp (node.network.endpoint ()));
		return result != nullptr;
	});
	ASSERT_TRUE (error);
	ASSERT_TRUE (system.nodes[0]->network.empty ());
}

// Tests the function network not_a_peer function used by the nano::transport::tcp_channels.insert ()
TEST (peer_container, reserved_ip_is_not_a_peer)
{
	nano::test::system system{ 1 };
	auto not_a_peer = [&node = system.nodes[0]] (nano::endpoint endpoint_a) -> bool {
		return node->network.not_a_peer (endpoint_a, true);
	};

	// The return value as true means an error because the IP address is for reserved use
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0x00000001)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xc0000201)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xc6336401)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xcb007101)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xe9fc0001)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xf0000001)), 10000))));
	ASSERT_TRUE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0xffffffff)), 10000))));

	// Test with a valid IP address
	ASSERT_FALSE (not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (0x08080808)), 10000))));
}

// Test the TCP channel cleanup function works properly. It is used to remove peers that are not
// exchanging messages after a while.
TEST (peer_container, tcp_channel_cleanup_works)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	// Set the keepalive period to avoid background messages affecting the last_packet_set time
	node_config.network_params.network.keepalive_period = std::chrono::minutes (10);
	nano::node_flags node_flags;
	// Want to test the cleanup function
	node_flags.disable_connection_cleanup = true;
	// Disable the confirm_req messages avoiding them to affect the last_packet_set time
	node_flags.disable_rep_crawler = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	auto outer_node1 = nano::test::add_outer_node (system, nano::test::get_available_port (), node_flags);
	outer_node1->config.network_params.network.keepalive_period = std::chrono::minutes (10);
	auto outer_node2 = nano::test::add_outer_node (system, nano::test::get_available_port (), node_flags);
	outer_node2->config.network_params.network.keepalive_period = std::chrono::minutes (10);
	auto now = std::chrono::steady_clock::now ();
	auto channel1 = nano::test::establish_tcp (system, node1, outer_node1->network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	// set the last packet sent for channel1 only to guarantee it contains a value.
	// it won't be necessarily the same use by the cleanup cutoff time
	node1.network.tcp_channels.modify (channel1, [&now] (auto channel) {
		channel->set_last_packet_sent (now - std::chrono::seconds (5));
	});
	auto channel2 = nano::test::establish_tcp (system, node1, outer_node2->network.endpoint ());
	ASSERT_NE (nullptr, channel2);
	// set the last packet sent for channel2 only to guarantee it contains a value.
	// it won't be necessarily the same use by the cleanup cutoff time
	node1.network.tcp_channels.modify (channel2, [&now] (auto channel) {
		channel->set_last_packet_sent (now + std::chrono::seconds (1));
	});
	ASSERT_EQ (2, node1.network.size ());
	ASSERT_EQ (2, node1.network.tcp_channels.size ());

	for (auto it = 0; node1.network.tcp_channels.size () > 1 && it < 10; ++it)
	{
		// we can't control everything the nodes are doing in background, so using the middle time as
		// the cutoff point.
		auto const channel1_last_packet_sent = channel1->get_last_packet_sent ();
		auto const channel2_last_packet_sent = channel2->get_last_packet_sent ();
		auto const max_last_packet_sent = std::max (channel1_last_packet_sent, channel2_last_packet_sent);
		auto const min_last_packet_sent = std::min (channel1_last_packet_sent, channel2_last_packet_sent);
		auto const cleanup_point = ((max_last_packet_sent - min_last_packet_sent) / 2) + min_last_packet_sent;

		node1.network.cleanup (cleanup_point);

		// it is possible that the last_packet_sent times changed because of another thread and the cleanup_point
		// is not the middle time anymore, in these case we wait a bit and try again in a loop up to 10 times
		if (node1.network.tcp_channels.size () == 2)
		{
			WAIT (500ms);
		}
	}
	ASSERT_EQ (1, node1.network.size ());
	ASSERT_EQ (1, node1.network.tcp_channels.size ());
}

TEST (channels, fill_random_clear)
{
	nano::test::system system (1);
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	system.nodes[0]->network.random_fill (target);
	ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}

// Test all targets get replaced by random_fill
TEST (channels, fill_random_full)
{
	nano::test::system system{ 1 };

	// create 4 peer nodes so that the random_fill is half real connections and half fillers
	for (int i = 0; i < 4; ++i)
	{
		auto outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, *system.nodes[0], outer_node->network.endpoint ()));
	}
	ASSERT_TIMELY_EQ (5s, 4, system.nodes[0]->network.tcp_channels.size ());

	// create an array of 8 endpoints with a known filler value
	auto filler_endpoint = nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), filler_endpoint);

	// random fill target array with endpoints taken from the network connections
	system.nodes[0]->network.random_fill (target);

	// check that all element in target got overwritten
	auto is_filler = [&filler_endpoint] (nano::endpoint const & endpoint_a) {
		return endpoint_a == filler_endpoint;
	};
	ASSERT_TRUE (std::none_of (target.begin (), target.end (), is_filler));
}

// Test only the known channels are filled
TEST (channels, fill_random_part)
{
	nano::test::system system{ 1 };
	std::array<nano::endpoint, 8> target;
	unsigned half{ target.size () / 2 };
	for (unsigned i = 0; i < half; ++i)
	{
		auto outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, *system.nodes[0], outer_node->network.endpoint ()));
	}
	ASSERT_EQ (half, system.nodes[0]->network.tcp_channels.size ());
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	system.nodes[0]->network.random_fill (target);
	ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000); }));
	ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::loopback (), 0); }));
	ASSERT_TRUE (std::all_of (target.begin () + half, target.end (), [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}

// TODO: remove node instantiation requirement for testing with bigger network size
TEST (peer_container, list_fanout)
{
	nano::test::system system{ 1 };
	auto node = system.nodes[0];
	ASSERT_EQ (0, node->network.size ());
	ASSERT_EQ (0.0, node->network.size_sqrt ());
	ASSERT_EQ (0, node->network.fanout ());
	ASSERT_TRUE (node->network.list (node->network.fanout ()).empty ());

	auto add_peer = [&node, &system] () {
		auto outer_node = nano::test::add_outer_node (system);
		auto channel = nano::test::establish_tcp (system, *node, outer_node->network.endpoint ());
	};

	add_peer ();
	ASSERT_TIMELY_EQ (5s, 1, node->network.size ());
	ASSERT_EQ (1.f, node->network.size_sqrt ());
	ASSERT_EQ (1, node->network.fanout ());
	ASSERT_EQ (1, node->network.list (node->network.fanout ()).size ());

	add_peer ();
	ASSERT_TIMELY_EQ (5s, 2, node->network.size ());
	ASSERT_EQ (std::sqrt (2.f), node->network.size_sqrt ());
	ASSERT_EQ (2, node->network.fanout ());
	ASSERT_EQ (2, node->network.list (node->network.fanout ()).size ());

	unsigned number_of_peers = 10;
	for (auto i = 2; i < number_of_peers; ++i)
	{
		add_peer ();
	}

	ASSERT_TIMELY_EQ (5s, number_of_peers, node->network.size ());
	ASSERT_EQ (std::sqrt (float (number_of_peers)), node->network.size_sqrt ());
	ASSERT_EQ (4, node->network.fanout ());
	ASSERT_EQ (4, node->network.list (node->network.fanout ()).size ());
}

// Test to make sure we don't repeatedly send keepalive messages to nodes that aren't responding
TEST (peer_container, reachout)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto & node1 = *system.add_node (node_flags);
	auto outer_node1 = nano::test::add_outer_node (system);
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, node1, outer_node1->network.endpoint ()));
	// Make sure having been contacted by them already indicates we shouldn't reach out
	ASSERT_TRUE (node1.network.reachout (outer_node1->network.endpoint ()));
	auto outer_node2 = nano::test::add_outer_node (system);
	ASSERT_FALSE (node1.network.reachout (outer_node2->network.endpoint ()));
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, node1, outer_node2->network.endpoint ()));
	// Reaching out to them once should signal we shouldn't reach out again.
	ASSERT_TRUE (node1.network.reachout (outer_node2->network.endpoint ()));
	// Make sure we don't purge new items
	node1.network.cleanup (std::chrono::steady_clock::now () - std::chrono::seconds (10));
	ASSERT_TRUE (node1.network.reachout (outer_node2->network.endpoint ()));
	// Make sure we purge old items
	node1.network.cleanup (std::chrono::steady_clock::now () + std::chrono::seconds (10));
	ASSERT_FALSE (node1.network.reachout (outer_node2->network.endpoint ()));
}

// This test is similar to network.filter_invalid_version_using with the difference that
// this one checks for the channel's connection to get stopped when an incoming message
// is from an outdated node version.
TEST (peer_container, depeer_on_outdated_version)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];

	// find the comms channel that goes from node2 to node1
	auto channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// send a keepalive, from node2 to node1, with the wrong version_using
	nano::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<uint8_t &> (keepalive.header.version_using) = nano::dev::network_params.network.protocol_version_min - 1;
	ASSERT_TIMELY (5s, channel->alive ());
	channel->send (keepalive);

	ASSERT_TIMELY (5s, !channel->alive ());
}
