#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace std::chrono_literals;

// A node never reaches out to its own endpoint, and a connection forced to it fails the handshake on its own node id
TEST (tcp_channels, no_self_incoming)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];

	// Its own endpoint is not a peer, so no reachout starts
	ASSERT_TRUE (node.network.not_a_peer (node.network.endpoint ()));
	ASSERT_FALSE (node.network.merge_peer (node.network.endpoint ()));

	// A connection forced to its own endpoint is dropped once the handshake shows its own node id, leaving no channel
	ASSERT_TRUE (node.network.tcp_channels.start_tcp (node.network.endpoint ()));
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_node_id) >= 1);
	ASSERT_TIMELY (5s, node.tcp_listener.all_sockets ().empty ());
	ASSERT_TRUE (node.network.empty ());
}

// Reserved addresses are not peers, public ones are
TEST (tcp_channels, reserved_ip_is_not_a_peer)
{
	nano::test::system system{ 1 };
	auto not_a_peer = [&node = system.nodes[0]] (nano::endpoint endpoint_a) -> bool {
		return node->network.not_a_peer (endpoint_a);
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

// Verify loopback-only mode rejects public peers before any reachout is attempted.
TEST (tcp_channels, disable_non_loopback_peers)
{
	nano::node_flags node_flags;
	node_flags.disable_non_loopback_peers = true;

	nano::test::system system;
	auto & node = *system.add_node (node_flags);

	// Use a concrete non-self loopback endpoint: port 0 is reserved, and self endpoints are rejected separately
	auto loopback_port = node.network.endpoint ().port () == 10000 ? 10001 : 10000;
	auto loopback = nano::endpoint (boost::asio::ip::address_v6::loopback (), loopback_port);
	auto v4_loopback = nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address_v4::loopback (), loopback_port));
	auto public_peer = nano::endpoint (boost::asio::ip::make_address_v6 ("2001:4860:4860::8888"), 10000);

	ASSERT_FALSE (node.network.not_a_peer (loopback));
	ASSERT_FALSE (node.network.not_a_peer (v4_loopback));
	ASSERT_TRUE (node.network.not_a_peer (public_peer));

	ASSERT_FALSE (node.network.track_reachout (public_peer));
	ASSERT_TRUE (node.network.track_reachout (loopback));
	ASSERT_TRUE (node.network.track_reachout (v4_loopback));
}

TEST (tcp_channels, fill_random_clear)
{
	nano::test::system system (1);
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	system.nodes[0]->network.random_fill (target);
	ASSERT_TRUE (std::all_of (target.begin (), target.end (), [] (nano::endpoint const & endpoint_a) { return endpoint_a == nano::endpoint (boost::asio::ip::address_v6::any (), 0); }));
}

// Test all targets get replaced by random_fill
TEST (tcp_channels, fill_random_full)
{
	nano::test::system system{ 1 };

	// create 8 peer nodes so that the random_fill is completely filled with real connection data
	for (int i = 0; i < 8; ++i)
	{
		auto outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, *system.nodes[0], outer_node->network.endpoint ()));
	}
	ASSERT_TIMELY_EQ (5s, 8, system.nodes[0]->network.tcp_channels.size ());

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
TEST (tcp_channels, fill_random_part)
{
	nano::test::system system{ 1 };
	std::array<nano::endpoint, 8> target;
	std::size_t half = target.size () / 2;
	for (std::size_t i = 0; i < half; ++i)
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

// The fanout never drops below the configured minimum and follows the natural log of the peer count above it
TEST (tcp_channels, fanout)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.network->minimum_fanout = 1;
	auto & node = *system.add_node (node_config);

	// Alone, the size log is zero and the minimum holds
	ASSERT_EQ (0, node.network.size ());
	ASSERT_FLOAT_EQ (0.0f, node.network.size_log ());
	ASSERT_EQ (1, node.network.fanout ());
	ASSERT_TRUE (node.network.list (node.network.fanout ()).empty ());

	// Three peers put the size log above the minimum, so the fanout is the log rounded up
	for (auto i = 0; i < 3; ++i)
	{
		system.add_node ();
	}
	ASSERT_TIMELY_EQ (5s, 3, node.network.size ());
	ASSERT_FLOAT_EQ (std::log (3.0f), node.network.size_log ());
	ASSERT_EQ (2, node.network.fanout ());
	ASSERT_EQ (2, node.network.list (node.network.fanout ()).size ());

	// The scale multiplies the fanout before it is rounded up
	ASSERT_EQ (1, node.network.fanout (0.5f));
	ASSERT_EQ (3, node.network.fanout (2.0f));
	ASSERT_EQ (3, node.network.list (node.network.fanout (2.0f)).size ());
}

// A reachout attempt is recorded once per endpoint, never for a connected peer, and again only once cleanup has purged the old attempt
TEST (tcp_channels, reachout)
{
	nano::test::system system;
	// Only the calls below may record attempts
	nano::node_flags node_flags;
	node_flags.disable_reachout = true;
	auto & node1 = *system.add_node (node_flags);

	// A connected peer is never reached out to
	auto outer_node1 = nano::test::add_outer_node (system);
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, node1, outer_node1->network.endpoint ()));
	ASSERT_FALSE (node1.network.track_reachout (outer_node1->network.endpoint ()));

	// A new endpoint is reached out to once, the pending attempt blocks a second
	auto outer_node2 = nano::test::add_outer_node (system);
	auto outer_node2_endpoint = outer_node2->network.endpoint ();
	ASSERT_TRUE (node1.network.track_reachout (outer_node2_endpoint));
	ASSERT_FALSE (node1.network.track_reachout (outer_node2_endpoint));

	// Once connected it is the channel that blocks a reachout
	ASSERT_NE (nullptr, nano::test::establish_tcp (system, node1, outer_node2_endpoint));
	ASSERT_FALSE (node1.network.track_reachout (outer_node2_endpoint));

	// A cleanup with a cutoff in the past keeps the channel
	node1.network.cleanup (std::chrono::steady_clock::now () - std::chrono::seconds (10));
	ASSERT_FALSE (node1.network.track_reachout (outer_node2_endpoint));

	// A cleanup that purges everything frees the endpoint for a new attempt
	outer_node1->stop ();
	outer_node2->stop ();
	node1.network.cleanup (std::chrono::steady_clock::now () + std::chrono::seconds (10));
	ASSERT_TIMELY (5s, node1.network.empty ());
	ASSERT_TRUE (node1.network.track_reachout (outer_node2_endpoint));
}

// The keepalive sweep wakes a channel idle for a keepalive period with a keepalive and leaves one that sent recently alone
TEST (tcp_channels, keepalive_idle)
{
	nano::test::system system;
	// A long keepalive period keeps the node's own sweep out of the way
	nano::node_config node_config = system.default_config ();
	node_config.network_params.network.keepalive_period = 10min;
	auto & node = *system.add_node (node_config);
	nano::node_config peer_config = system.default_config ();
	peer_config.network_params.network.keepalive_period = 10min;
	auto & peer = *system.add_node (peer_config);
	auto const channel = node.network.find_node_id (peer.get_node_id ());
	ASSERT_NE (nullptr, channel);
	auto const received = [&peer] () {
		return peer.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	};

	// The one keepalive sent on connect has arrived and a sweep now sends nothing
	ASSERT_TIMELY_EQ (5s, 1, received ());
	node.network.tcp_channels.keepalive ();
	ASSERT_ALWAYS_EQ (100ms, 1, received ());

	// Once the channel has been idle for longer than the period, a sweep wakes it
	channel->set_last_packet_sent (std::chrono::steady_clock::now () - 11min);
	node.network.tcp_channels.keepalive ();
	ASSERT_TIMELY_EQ (5s, 2, received ());
}
