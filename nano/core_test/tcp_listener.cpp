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

/*
 * A connect whose handshake makes a channel reports success to its callback with an empty error code, and not before the channel is there
 */
TEST (tcp_listener, connect_callback_reports_success)
{
	nano::test::system system;
	// Neither node reaches out on its own, so the only connection is the one made below
	nano::node_flags node_flags;
	node_flags.disable_reachout = true;
	auto node = system.add_node (node_flags);
	auto const peer = system.make_disconnected_node (system.default_config (), node_flags);
	auto const target = peer->tcp_listener.endpoint ();

	// The callback receives the outcome once the handshake is over
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	ASSERT_TIMELY (5s, result.is_set ());

	// Success carries no error, and the channel it stands for exists
	auto const [ec] = result.get ();
	ASSERT_FALSE (ec);
	ASSERT_NE (nullptr, node->network.find_node_id (peer->get_node_id ()));
}

/*
 * A connect to a port nothing listens on reports the network error that ended it, outside the category of refusals by this node
 */
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

	// The attempt starts and its callback receives the failure
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (dead_endpoint.address (), dead_endpoint.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	ASSERT_TIMELY (5s, result.is_set ());

	// The error comes from the connect itself, not from a refusal by this node
	auto const [ec] = result.get ();
	ASSERT_TRUE (ec);
	ASSERT_NE (nano::error_network_category (), ec.category ());
	ASSERT_FALSE (ec.message ().empty ());
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

	// Open as many outbound connections as the inbound limit; the peer never handshakes, so the listener's own count tells when they are up
	for (auto idx = 0; idx < 2; ++idx)
	{
		node->tcp_listener.connect (target.address (), target.port ());
	}
	ASSERT_TIMELY_EQ (5s, 2, node->tcp_listener.connection_count (nano::transport::connection_type::outbound));

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

	// Opens a client connection to the node, which the node either accepts or refuses
	auto connect = [&node] () {
		return std::make_unique<nano::test::peer> (*node);
	};

	// The first client takes the only slot
	auto client1 = connect ();
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success));

	// Closing it evicts the connection but keeps the slot occupied
	client1->close ();
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead));
	ASSERT_EQ (0, node->tcp_listener.connection_count ());

	// A client arriving inside the cooldown is refused as one too many for the IP
	auto client2 = connect ();
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip));
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success));

	// Connects a fresh client and waits for the node's verdict on it, true when it was accepted
	std::deque<std::unique_ptr<nano::test::peer>> clients;
	auto try_connect = [&] () {
		auto const accepted_before = node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success);
		auto const rejected_before = node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip);
		clients.push_back (connect ());
		while (node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success) == accepted_before && node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip) == rejected_before)
		{
			if (system.poll ())
			{
				return false; // Deadline passed, the loop below reports it
			}
		}
		return node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success) > accepted_before;
	};

	// Fresh clients keep trying until the cooldown has expired and one of them is accepted
	system.deadline_set (10s);
	while (!try_connect ())
	{
		ASSERT_NO_ERROR (system.poll (100ms));
	}
	ASSERT_EQ (2, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success));
}

/*
 * The cooldown applies to connections this node opens as well: after it closes its own outbound connection, a connect to the same address inside the window is refused before any socket is opened, its callback naming the per IP limit, and goes through again once the window has passed
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

	// The first attempt takes the only slot; the peer never handshakes, so the listener's own count tells when the connection is up
	node->tcp_listener.connect (target.address (), target.port ());
	ASSERT_TIMELY_EQ (5s, 1, node->tcp_listener.connection_count (nano::transport::connection_type::outbound));

	// Closing the connection evicts it but keeps the slot occupied
	auto sockets = node->tcp_listener.all_sockets ();
	ASSERT_EQ (1, sockets.size ());
	sockets.front ()->close ();
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::erase_dead));
	ASSERT_EQ (0, node->tcp_listener.connection_count ());

	// An attempt inside the cooldown is refused before any socket is opened, and its callback says why
	nano::test::shared_result<std::error_code> refused;
	node->tcp_listener.connect (target.address (), target.port (), [refused] (nano::tcp_endpoint const &, std::error_code ec) {
		refused.set (ec);
	});
	ASSERT_TIMELY (5s, refused.is_set ());
	auto const [refused_ec] = refused.get ();
	ASSERT_EQ (nano::error_network::max_connections_per_ip, refused_ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::out));
	ASSERT_EQ (0, node->tcp_listener.attempt_count ());

	// Starts a connect and waits for the listener's verdict on it, true when the connection went through
	auto try_connect = [&] () {
		auto const connected_before = node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out);
		auto const rejected_before = node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::out);
		node->tcp_listener.connect (target.address (), target.port ());
		while (node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out) == connected_before && node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip, nano::stat::dir::out) == rejected_before)
		{
			if (system.poll ())
			{
				return false; // Deadline passed, the loop below reports it
			}
		}
		return node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out) > connected_before;
	};

	// Fresh attempts keep trying until the cooldown has expired and one of them goes through
	system.deadline_set (10s);
	while (!try_connect ())
	{
		ASSERT_NO_ERROR (system.poll (100ms));
	}
	ASSERT_EQ (2, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out));
}

/*
 * A connect whose handshake ends without a channel reports the reason to its callback: a node that connects to its own listener learns that the peer has its own id
 */
TEST (tcp_listener, connect_callback_reports_own_node_id)
{
	nano::test::system system{ 1 };
	auto node = system.nodes[0];
	auto const target = node->tcp_listener.endpoint ();

	// The TCP connection to the node's own listener goes through, the handshake over it cannot
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	ASSERT_TIMELY (5s, result.is_set ());

	// The callback names what ended the handshake, and no channel was made
	auto const [ec] = result.get ();
	ASSERT_EQ (nano::error_handshake::own_node_id, ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::connect_success, nano::stat::dir::out));
	ASSERT_EQ (0, node->network.size ());
}

/*
 * A handshake the peer never answers is reported as a timeout, once the handshake timeout has closed the connection
 */
TEST (tcp_listener, connect_callback_reports_handshake_timeout)
{
	nano::test::system system;
	// A handshake gets two seconds
	nano::node_config node_config = system.default_config ();
	node_config.tcp->handshake_timeout = 2s;
	auto node = system.add_node (node_config);

	// A listening peer nobody accepts from: the connection is made and the node's query stays unanswered
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});

	// The listener gives up on the handshake and the callback names the timeout
	ASSERT_TIMELY (10s, result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_EQ (nano::error_handshake::timeout, ec);
	ASSERT_EQ (0, node->network.size ());
}

/*
 * A handshake message stamped for another network ends the handshake, and the callback says the peer is on a different network
 */
TEST (tcp_listener, connect_callback_reports_wrong_network)
{
	nano::test::system system;
	auto node = system.add_node ();
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();

	// The node connects out and the peer greets it with a query stamped for the live network
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	auto const peer = listener.accept ();
	ASSERT_NE (nullptr, peer);
	auto query = nano::test::handshake_v3 ().query ();
	query.header.network = nano::network_type::nano_live_network;
	ASSERT_NE (query.header.network, node->network_params.network.current_network);
	ASSERT_TRUE (peer->send (query));

	// The callback names the reason
	ASSERT_TIMELY (5s, result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_EQ (nano::error_handshake::wrong_network, ec);
	ASSERT_EQ (0, node->network.size ());
}

/*
 * A handshake with a peer the node already has a channel to ends without a second channel, and the callback says the peer is already connected
 */
TEST (tcp_listener, connect_callback_reports_duplicate_channel)
{
	nano::test::system system;
	auto node = system.add_node ();
	nano::keypair id;

	// A peer with the id connects to the node first
	nano::test::peer connected{ *node };
	ASSERT_TRUE (connected.handshake (nano::test::handshake_v3 ({ .node_id = id })));
	ASSERT_EQ (1, node->network.size ());

	// The node then connects out to a peer that answers its query under the same id
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	auto const peer = listener.accept ();
	ASSERT_NE (nullptr, peer);
	ASSERT_TIMELY_EQ (5s, 1, peer->received<nano::messages::node_id_handshake> ().size ());
	auto const opening = peer->received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (opening.query.has_value ());
	ASSERT_TRUE (peer->send (nano::test::handshake_v3 ({ .node_id = id }).response (opening.query->cookie)));

	// The callback names the reason and the first channel stays the only one
	ASSERT_TIMELY (5s, result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_EQ (nano::error_handshake::channel_duplicate, ec);
	ASSERT_EQ (1, node->network.size ());
}

/*
 * A peer that hangs up in the middle of the handshake ends the attempt with the socket's own error, which is neither a refusal by this node nor a handshake reason
 */
TEST (tcp_listener, connect_callback_reports_hangup)
{
	nano::test::system system;
	auto node = system.add_node ();
	nano::test::peer_listener listener{ *node };
	auto const target = listener.endpoint ();

	// The node connects out and the peer closes the connection without answering
	nano::test::shared_result<std::error_code> result;
	node->tcp_listener.connect (target.address (), target.port (), [result] (nano::tcp_endpoint const &, std::error_code ec) {
		result.set (ec);
	});
	auto const peer = listener.accept ();
	ASSERT_NE (nullptr, peer);
	peer->close ();

	// The callback receives the error the socket ended with
	ASSERT_TIMELY (5s, result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_TRUE (ec);
	ASSERT_NE (nano::error_handshake_category (), ec.category ());
	ASSERT_NE (nano::error_network_category (), ec.category ());
}
