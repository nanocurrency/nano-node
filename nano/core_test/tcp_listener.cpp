#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <memory>

using namespace std::chrono_literals;

/*
 * At the inbound connection cap a further peer is left waiting instead of being refused, and it is accepted once one of the connections closes.
 * The inbound count never exceeds the cap
 */
TEST (tcp_listener, max_connections)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.tcp->max_inbound_connections = 2;
	auto & node = *system.add_node (config);

	// Two peers take both slots, a third connects at the TCP level but is not accepted
	std::deque<nano::test::peer> peers;
	for (int i = 0; i < 3; ++i)
	{
		peers.emplace_back (node);
	}
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));
	ASSERT_ALWAYS_EQ (500ms, 2, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_rejected, nano::stat::dir::in));
	ASSERT_TRUE (peers.back ().alive ());

	// Closing the first peer frees its slot once the listener purges the dead connection, and the third one gets in
	peers.front ().close ();
	ASSERT_TIMELY_EQ (5s, 3, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));
	ASSERT_EQ (2, node.tcp_listener.connection_count (nano::transport::connection_type::inbound));
}

/*
 * One peer more than the per IP limit allows is refused: the listener counts the refusal and closes the connection, while the others stay accepted
 */
TEST (tcp_listener, max_connections_per_ip)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.network->max_peers_per_ip = 3;
	auto & node = *system.add_node (config);

	// Every peer connects from the loopback address, the last one past the limit
	std::deque<nano::test::peer> peers;
	for (int i = 0; i < 4; ++i)
	{
		peers.emplace_back (node);
	}
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::in));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));

	// The refused peer finds its connection closed
	ASSERT_TIMELY (5s, !peers.back ().alive ());
	ASSERT_EQ (3, node.tcp_listener.connection_count (nano::transport::connection_type::inbound));
}

/*
 * One peer more than the per subnetwork limit allows is refused and its connection closed. The per IP limit is off,
 * since every peer connects from the same address and so also from the same subnetwork
 */
TEST (tcp_listener, max_connections_per_subnetwork)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_max_peers_per_ip = true;
	nano::node_config config = system.default_config ();
	config.network->max_peers_per_subnetwork = 3;
	auto & node = *system.add_node (config, flags);

	// The last peer is past the subnetwork limit
	std::deque<nano::test::peer> peers;
	for (int i = 0; i < 4; ++i)
	{
		peers.emplace_back (node);
	}
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_subnetwork, nano::stat::dir::in));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));

	// The refused peer finds its connection closed
	ASSERT_TIMELY (5s, !peers.back ().alive ());
	ASSERT_EQ (3, node.tcp_listener.connection_count (nano::transport::connection_type::inbound));
}

/*
 * With the per IP limit disabled by its flag, peers past the configured limit are all accepted
 */
TEST (tcp_listener, max_connections_per_ip_disabled)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.disable_max_peers_per_ip = true;
	nano::node_config config = system.default_config ();
	config.network->max_peers_per_ip = 3;
	auto & node = *system.add_node (config, flags);

	// One more peer than the limit would allow, all accepted
	std::deque<nano::test::peer> peers;
	for (int i = 0; i < 4; ++i)
	{
		peers.emplace_back (node);
	}
	ASSERT_TIMELY_EQ (5s, 4, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::in));
}

/*
 * A peer that connects and never starts the handshake is disconnected at the handshake timeout, which is counted
 */
TEST (tcp_listener, handshake_timeout)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.tcp->handshake_timeout = 2s;
	auto & node = *system.add_node (config);

	// A peer that connects and sends nothing
	nano::test::peer peer{ node };
	ASSERT_TIMELY_EQ (5s, 1, node.tcp_listener.connection_count ());

	// The listener closes the connection for the timeout
	ASSERT_TIMELY (10s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::handshake_timeout));
	ASSERT_TIMELY_EQ (5s, 0, node.tcp_listener.connection_count ());
}

/*
 * A peer that sends a handshake query but never answers the node's own query is disconnected at the handshake timeout, which is counted
 */
TEST (tcp_listener, handshake_timeout_after_query)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.tcp->handshake_timeout = 2s;
	auto & node = *system.add_node (config);

	// A peer that opens the handshake and goes quiet once the node has answered
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.send (nano::test::handshake_v3 ().query ()));
	ASSERT_TIMELY_EQ (5s, 1, peer.received<nano::messages::node_id_handshake> ().size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake, nano::stat::dir::in));

	// The node waits for a response that never comes, until the timeout closes the connection
	ASSERT_TIMELY (10s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_listener, nano::stat::detail::handshake_timeout));
	ASSERT_TIMELY_EQ (5s, 0, node.tcp_listener.connection_count ());
}

// The connect callback reports the outcome of an outbound connection attempt; used to surface preconfigured peer failures
TEST (tcp_listener, connect_callback_reports_success)
{
	nano::test::system system;
	nano::node_flags node_flags;
	// Disable per-ip/subnetwork limits so the self-connection is always accepted (no dependency on connection counts)
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = true;
	auto node = system.add_node (node_flags);

	auto target = node->tcp_listener.endpoint ();
	std::atomic<bool> called{ false };
	std::atomic<bool> reported_error{ true };

	bool initiated = node->tcp_listener.connect (target.address (), target.port (), [&] (nano::tcp_endpoint const &, std::error_code ec) {
		reported_error = static_cast<bool> (ec);
		called = true;
	});
	ASSERT_TRUE (initiated);

	ASSERT_TIMELY (5s, called.load ());
	ASSERT_FALSE (reported_error.load ()); // Success => empty error code
}

TEST (tcp_listener, connect_callback_reports_failure)
{
	nano::test::system system{ 1 };
	auto node = system.nodes[0];

	// Reserve then immediately release a loopback port so nothing is listening on it
	boost::asio::ip::tcp::endpoint dead_endpoint;
	{
		boost::asio::io_context io_ctx;
		boost::asio::ip::tcp::acceptor acceptor{ io_ctx, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } };
		dead_endpoint = acceptor.local_endpoint ();
	}

	std::atomic<bool> called{ false };
	std::atomic<bool> reported_error{ false };
	std::string reported_message;

	bool initiated = node->tcp_listener.connect (dead_endpoint.address (), dead_endpoint.port (), [&] (nano::tcp_endpoint const &, std::error_code ec) {
		// Written before the atomic flag, so it is visible once `called` is observed true
		reported_message = ec.message ();
		reported_error = static_cast<bool> (ec);
		called = true;
	});
	ASSERT_TRUE (initiated);

	ASSERT_TIMELY (5s, called.load ());
	ASSERT_TRUE (reported_error.load ());
	ASSERT_FALSE (reported_message.empty ());
}

/*
 * Outbound connections do not consume the inbound connection budget: with as many of them open as the inbound cap, inbound peers up to the cap are still accepted
 */
TEST (tcp_listener, max_connections_excludes_outbound)
{
	nano::test::system system;

	nano::node_flags node_flags;
	// All connections share the loopback address
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = true;
	nano::node_config node_config = system.default_config ();
	node_config.tcp->max_inbound_connections = 2;
	auto node = system.add_node (node_config, node_flags);

	// A listening peer nobody accepts from, the kernel completes the handshakes and keeps the outbound connections open
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();

	// Open as many outbound connections as the inbound limit
	std::atomic<size_t> outbound_connected = 0;
	for (auto idx = 0; idx < 2; ++idx)
	{
		ASSERT_TRUE (node->tcp_listener.connect (target.address (), target.port (), [&outbound_connected] (nano::tcp_endpoint const &, std::error_code ec) {
			ASSERT_FALSE (ec);
			++outbound_connected;
		}));
	}
	ASSERT_TIMELY_EQ (5s, outbound_connected, 2);
	ASSERT_EQ (node->tcp_listener.connection_count (nano::transport::connection_type::outbound), 2);

	// Inbound connections up to the limit are still accepted
	std::deque<nano::test::peer> peers;
	for (auto idx = 0; idx < 2; ++idx)
	{
		peers.emplace_back (*node);
	}
	ASSERT_TIMELY_EQ (5s, node->tcp_listener.connection_count (nano::transport::connection_type::inbound), 2);
}

/*
 * A closed inbound connection keeps counting toward the per IP limit for the reconnect cooldown: a client that connects, disconnects and comes back inside the window is refused, and admitted again once the window has passed
 */
TEST (tcp_listener, reconnect_cooldown_inbound)
{
	nano::test::system system;

	// One connection per IP, and a closed one keeps its slot for a few seconds
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 1;
	node_config.tcp->reconnect_cooldown = 3s;
	auto node = system.add_node (node_config);

	auto connect = [&node] () {
		return std::make_unique<nano::test::peer> (*node);
	};
	auto accepted_count = [&node] () {
		return node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success);
	};
	auto rejected_count = [&node] () {
		return node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip);
	};

	// The first client takes the only slot
	auto client1 = connect ();
	ASSERT_TIMELY_EQ (5s, accepted_count (), 1);

	// Closing it evicts the connection but keeps the slot occupied
	client1->close ();
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead), 1);
	ASSERT_EQ (node->tcp_listener.connection_count (), 0);

	// A client arriving inside the cooldown is refused as one too many for the IP
	auto client2 = connect ();
	ASSERT_TIMELY_EQ (5s, rejected_count (), 1);
	ASSERT_EQ (accepted_count (), 1);

	// Fresh clients keep trying until the cooldown has expired and one of them is accepted
	std::deque<std::unique_ptr<nano::test::peer>> clients;
	auto try_connect = [&] () {
		auto const accepted_before = accepted_count ();
		auto const rejected_before = rejected_count ();
		clients.push_back (connect ());
		while (accepted_count () == accepted_before && rejected_count () == rejected_before)
		{
			if (system.poll ())
			{
				return false; // Deadline passed, the loop below reports it
			}
		}
		return accepted_count () > accepted_before;
	};
	system.deadline_set (10s);
	while (!try_connect ())
	{
		ASSERT_NO_ERROR (system.poll (100ms));
	}
	ASSERT_EQ (accepted_count (), 2);
}

/*
 * The cooldown applies to connections this node opens as well: after it closes its own outbound connection, a connect to the same address inside the window is refused before any socket is opened, and goes through again once the window has passed
 */
TEST (tcp_listener, reconnect_cooldown_outbound)
{
	nano::test::system system;

	// One connection per IP, and a closed one keeps its slot for a few seconds
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 1;
	node_config.tcp->reconnect_cooldown = 3s;
	auto node = system.add_node (node_config);

	// A listening peer nobody accepts from, so no handshake or reachout adds connections of its own
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();

	// Every attempt that gets through reports success to its callback
	std::atomic<size_t> connected{ 0 };
	auto connect = [&] () {
		return node->tcp_listener.connect (target.address (), target.port (), [&connected] (nano::tcp_endpoint const &, std::error_code ec) {
			ASSERT_FALSE (ec);
			++connected;
		});
	};

	// The first attempt takes the only slot
	ASSERT_TRUE (connect ());
	ASSERT_TIMELY_EQ (5s, connected.load (), 1);

	// Closing the connection evicts it but keeps the slot occupied
	auto sockets = node->tcp_listener.all_sockets ();
	ASSERT_EQ (sockets.size (), 1);
	sockets.front ()->close ();
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead), 1);
	ASSERT_EQ (node->tcp_listener.connection_count (), 0);

	// An attempt inside the cooldown is refused before any socket is opened
	ASSERT_FALSE (connect ());
	ASSERT_EQ (node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::out), 1);
	ASSERT_EQ (connected.load (), 1);

	// Once the cooldown has expired an attempt goes through again
	ASSERT_TIMELY (10s, connect ());
	ASSERT_TIMELY_EQ (5s, connected.load (), 2);
}

/*
 * Stopping the listener cancels the accept it is waiting on, which ends the accept loop without being counted as a failed accept
 */
TEST (tcp_listener, stop_cancels_accept)
{
	nano::test::system system;
	auto node = system.add_node ();

	// With no peer connecting, the accept loop stays in its first accept from the moment the listener starts
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success, nano::stat::dir::in));

	// Stopping the node stops the listener, which cancels that accept
	system.stop_node (*node);

	// The cancelled accept is not an accept error
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_error, nano::stat::dir::in));
}
