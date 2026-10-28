#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace std::chrono_literals;

/*
 * A node never reaches out to its own endpoint, and a connection forced to it fails the handshake on its own node id
 */
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

/*
 * Addresses from the reserved ranges are not peers, a public one is
 */
TEST (tcp_channels, reserved_ip_is_not_a_peer)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	auto const not_a_peer = [&node] (uint32_t address) {
		return node.network.not_a_peer (nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address (boost::asio::ip::address_v4 (address)), 10000)));
	};

	// One address each from this network, the three documentation nets, multicast and the reserved 240/4 block up to broadcast
	for (auto const address : { 0x00000001u, 0xc0000201u, 0xc6336401u, 0xcb007101u, 0xe9fc0001u, 0xf0000001u, 0xffffffffu })
	{
		ASSERT_TRUE (not_a_peer (address));
	}

	// A public address is a peer
	ASSERT_FALSE (not_a_peer (0x08080808u));
}

/*
 * With non loopback peers disabled, loopback endpoints are peers and a public one is refused before any reachout
 */
TEST (tcp_channels, disable_non_loopback_peers)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_non_loopback_peers = true;
	auto & node = *system.add_node (node_flags);
	// A loopback port other than the node's own, since port zero is reserved and the node's own endpoint is refused on its own account
	auto const loopback_port = node.network.endpoint ().port () == 10000 ? 10001 : 10000;
	auto const loopback = nano::endpoint (boost::asio::ip::address_v6::loopback (), loopback_port);
	auto const v4_loopback = nano::transport::map_endpoint_to_v6 (nano::endpoint (boost::asio::ip::address_v4::loopback (), loopback_port));
	auto const public_peer = nano::endpoint (boost::asio::ip::make_address_v6 ("2001:4860:4860::8888"), 10000);

	// Both loopback forms are peers, the public address is not
	ASSERT_FALSE (node.network.not_a_peer (loopback));
	ASSERT_FALSE (node.network.not_a_peer (v4_loopback));
	ASSERT_TRUE (node.network.not_a_peer (public_peer));

	// Reachout tracking draws the same line
	ASSERT_FALSE (node.network.track_reachout (public_peer));
	ASSERT_TRUE (node.network.track_reachout (loopback));
	ASSERT_TRUE (node.network.track_reachout (v4_loopback));
}

/*
 * With no peers random_fill blanks every slot
 */
TEST (tcp_channels, fill_random_clear)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::endpoint const unspecified{ boost::asio::ip::address_v6::any (), 0 };

	// Every slot comes back unspecified, the filler included
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000));
	node.network.random_fill (target);
	ASSERT_TRUE (std::all_of (target.begin (), target.end (), [&unspecified] (auto const & slot) { return slot == unspecified; }));
}

/*
 * With at least as many peers as slots, random_fill overwrites every slot with a peer's endpoint
 */
TEST (tcp_channels, fill_random_full)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];

	// Eight peers, one per slot
	for (auto i = 0; i < 8; ++i)
	{
		auto const outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, node, outer_node->network.endpoint ()));
	}
	ASSERT_TIMELY_EQ (5s, 8, node.network.size ());

	// No slot keeps its filler
	auto const filler = nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::array<nano::endpoint, 8> target;
	std::fill (target.begin (), target.end (), filler);
	node.network.random_fill (target);
	ASSERT_TRUE (std::none_of (target.begin (), target.end (), [&filler] (auto const & slot) { return slot == filler; }));
}

/*
 * With fewer peers than slots, random_fill puts the peers first and blanks the rest
 */
TEST (tcp_channels, fill_random_part)
{
	nano::test::system system{ 1 };
	auto & node = *system.nodes[0];
	std::array<nano::endpoint, 8> target;
	auto const half = target.size () / 2;
	auto const filler = nano::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	nano::endpoint const unspecified{ boost::asio::ip::address_v6::any (), 0 };

	// Four peers for eight slots
	for (std::size_t i = 0; i < half; ++i)
	{
		auto const outer_node = nano::test::add_outer_node (system);
		ASSERT_NE (nullptr, nano::test::establish_tcp (system, node, outer_node->network.endpoint ()));
	}
	ASSERT_EQ (half, node.network.size ());

	// The first four slots hold a peer each, with its real port, the last four are unspecified
	std::fill (target.begin (), target.end (), filler);
	node.network.random_fill (target);
	ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [&filler] (auto const & slot) { return slot == filler; }));
	ASSERT_TRUE (std::none_of (target.begin (), target.begin () + half, [] (auto const & slot) { return slot == nano::endpoint (boost::asio::ip::address_v6::loopback (), 0); }));
	ASSERT_TRUE (std::all_of (target.begin () + half, target.end (), [&unspecified] (auto const & slot) { return slot == unspecified; }));
}

/*
 * The fanout never drops below the configured minimum and follows the natural log of the peer count above it
 */
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

/*
 * A reachout attempt is recorded once per endpoint, never for a connected peer, and again only once cleanup has purged the old attempt
 */
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

/*
 * The keepalive sweep wakes the channels that have sent nothing since its cutoff and leaves the others alone
 */
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
	ASSERT_NE (nullptr, node.network.find_node_id (peer.get_node_id ()));
	auto const received = [&peer] () {
		return peer.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	};

	// The one keepalive sent on connect has arrived and a sweep with the node's own cutoff sends nothing
	ASSERT_TIMELY_EQ (5s, 1, received ());
	node.network.tcp_channels.keepalive (std::chrono::steady_clock::now () - node_config.network_params.network.keepalive_period);
	ASSERT_ALWAYS_EQ (100ms, 1, received ());

	// A cutoff later than any send the channel can have made marks it idle, so the sweep wakes it
	node.network.tcp_channels.keepalive (std::chrono::steady_clock::now () + 1h);
	ASSERT_TIMELY_EQ (5s, 2, received ());
}
