#include <nano/node/socket.hpp>
#include <nano/node/transport/tcp.hpp>
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

TEST (peer_container, no_recontact)
{
	nano::test::system system{ 1 };
	auto & node1 = *system.nodes[0];
	nano::network & network{ node1.network };
	unsigned observed_peer{ 0 };
	auto observed_disconnect{ false };
	ASSERT_EQ (0, network.size ());
	network.channel_observer = [&observed_peer] (std::shared_ptr<nano::transport::channel> const &) { ++observed_peer; };
	node1.network.disconnect_observer = [&observed_disconnect] () { observed_disconnect = true; };
	auto outer_node = nano::test::add_outer_node (system);
	auto channel = nano::test::establish_tcp (system, node1, outer_node->network.endpoint ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (1, network.size ());
	node1.network.cleanup (std::chrono::steady_clock::now () + std::chrono::seconds (5));
	ASSERT_TRUE (network.empty ());
	ASSERT_EQ (1, observed_peer);
	ASSERT_TRUE (observed_disconnect);
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

// Tests the function nano::transport::tcp_channels.insert () doesn't accept peers from the reserved addresses list
TEST (peer_container, reserved_peers_no_contact)
{
	nano::test::system system{ 1 };
	auto & channels = system.nodes[0]->network.tcp_channels;
	auto insert_channel = [&node = *system.nodes[0], &channels] (nano::endpoint endpoint_a) -> bool {
		// Create dummy socket and channel only for passing the IP address
		auto ignored_socket = std::make_shared<nano::server_socket> (node, nano::transport::map_endpoint_to_tcp (endpoint_a), 10);
		auto ignored_channel = std::make_shared<nano::transport::channel_tcp> (node, ignored_socket->weak_from_this ());
		return channels.insert (ignored_channel, std::shared_ptr<nano::socket> (), std::shared_ptr<nano::bootstrap_server> ());
	};

	// The return value as true means an error.
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0x00000001)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xc0000201)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xc6336401)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xcb007101)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xe9fc0001)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xf0000001)), 10000)));
	ASSERT_TRUE (insert_channel (nano::endpoint (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (0xffffffff)), 10000)));
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

// TODO: Fix the failing test
TEST (peer_container, split)
{
	nano::test::system system;
	nano::node_config node_config (nano::test::get_available_port (), system.logging);
	node_config.network_params.network.cleanup_period = std::chrono::minutes (10);
	nano::node_flags node_flags;
	node_flags.disable_connection_cleanup = true;
	auto & node1 = *system.add_node (node_config, node_flags);
	auto outer_node1 = nano::test::add_outer_node (system);
	auto outer_node2 = nano::test::add_outer_node (system);
	auto now = std::chrono::steady_clock::now ();
	auto channel1 = nano::test::establish_tcp (system, node1, outer_node1->network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	node1.network.tcp_channels.modify (channel1, [&now] (auto channel) {
		channel->set_last_packet_sent (now - std::chrono::seconds (5));
	});
	auto channel2 = nano::test::establish_tcp (system, node1, outer_node2->network.endpoint ());
	ASSERT_NE (nullptr, channel2);
	node1.network.tcp_channels.modify (channel2, [&now] (auto channel) {
		channel->set_last_packet_sent (now + std::chrono::seconds (1));
	});
	ASSERT_EQ (2, node1.network.size ());
	ASSERT_EQ (2, node1.network.tcp_channels.size ());
	node1.network.cleanup (now);
	ASSERT_EQ (1, node1.network.size ());
	ASSERT_EQ (1, node1.network.tcp_channels.size ());
	auto list = node1.network.list (1);
	ASSERT_EQ (outer_node2->network.endpoint (), list[0]->get_endpoint ());
}

TEST (channels, fill_random_clear)
{
	nano::test::system system (1);
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	system.nodes[0]->network.random_fill (target);
	ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}

// Test all targets get replaced
TEST (channels, fill_random_full)
{
	nano::test::system system{ 1 };
	unsigned network_size{ 20 };
	for (uint16_t i (0u); i < network_size; ++i)
	{
		auto outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, *system.nodes[0], outer_node->network.endpoint ()));
	}
	ASSERT_EQ (network_size, system.nodes[0]->network.tcp_channels.size ());
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	system.nodes[0]->network.random_fill (target);
	ASSERT_TRUE (std::none_of (target.begin (), target.end (), [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000); }));
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
	auto list1 = node->network.list (node->network.fanout ());
	ASSERT_TRUE (list1.empty ());
	auto add_peer = [&node, &system] (uint16_t const port_a) {
		auto outer_node = nano::test::add_outer_node (system);
		auto channel = nano::test::establish_tcp (system, *node, outer_node->network.endpoint ());
	};
	add_peer (9998);
	ASSERT_EQ (1, node->network.size ());
	ASSERT_EQ (1.f, node->network.size_sqrt ());
	ASSERT_EQ (1, node->network.fanout ());
	auto list2 = node->network.list (node->network.fanout ());
	ASSERT_EQ (1, list2.size ());
	add_peer (9999);
	ASSERT_EQ (2, node->network.size ());
	ASSERT_EQ (std::sqrt (2.f), node->network.size_sqrt ());
	ASSERT_EQ (2, node->network.fanout ());
	auto list3 = node->network.list (node->network.fanout ());
	ASSERT_EQ (2, list3.size ());
	// The previous version of this test used 1000 peers. Reduced to 10 due to the use of node instances
	unsigned number_of_peers{ 20 };
	for (auto i = 0; i < number_of_peers; ++i)
	{
		add_peer (10000 + i);
	}
	number_of_peers += 2;
	ASSERT_EQ (number_of_peers, node->network.size ());
	ASSERT_EQ (std::sqrt (float (number_of_peers)), node->network.size_sqrt ());
	auto expected_size (static_cast<size_t> (std::ceil (std::sqrt (float (number_of_peers)))));
	ASSERT_EQ (expected_size, node->network.fanout ());
	auto list4 = node->network.list (node->network.fanout ());
	ASSERT_EQ (expected_size, list4.size ());
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

// TODO: fix failing test
// This test checks if a packet is discarded when its protocol version is outdated
TEST (peer_container, depeer)
{
	nano::test::system system{ 1 };
	auto network_constants = nano::dev::network_params.network;
	const_cast<uint8_t &> (network_constants.protocol_version) = 1;
	nano::keepalive message{ network_constants };
	auto bytes = message.to_bytes ();
	ASSERT_EQ (1, message.header.version_using);
	auto outer_node = nano::test::add_outer_node (system);
	auto channel = nano::test::establish_tcp (system, *system.nodes[0], outer_node->network.endpoint ());
	ASSERT_NE (nullptr, channel);
	system.nodes[0]->network.tcp_channels.start_tcp_receive_node_id (channel, channel->get_endpoint (), message.to_bytes ());
	ASSERT_TIMELY (6s, 1 == system.nodes[0]->stats.count (nano::stat::type::message, nano::stat::detail::outdated_version));
}
