#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/boost/asio/ip/network_v6.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/message_header.hpp>
#include <nano/messages/message_type.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <future>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

TEST (tcp_listener, max_connections)
{
	nano::test::system system;

	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.tcp->max_inbound_connections = 2;
	auto node = system.add_node (node_config, node_flags);

	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// Start 3 clients, 2 should connect successfully
	auto client1 = std::make_shared<nano::transport::tcp_socket> (*node);
	client1->async_connect (node->network.endpoint (), connect_handler);

	auto client2 = std::make_shared<nano::transport::tcp_socket> (*node);
	client2->async_connect (node->network.endpoint (), connect_handler);

	auto client3 = std::make_shared<nano::transport::tcp_socket> (*node);
	client3->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 3);
	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 2);
	ASSERT_ALWAYS_EQ (1s, node->tcp_listener.all_sockets ().size (), 2);
}

TEST (tcp_listener, max_connections_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);
	ASSERT_FALSE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network->max_peers_per_ip;
	ASSERT_GE (max_ip_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_ip_connections);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip), 1);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_ip_connections + 1);
}

TEST (tcp_listener, max_connections_per_subnetwork)
{
	nano::test::system system;

	nano::node_flags node_flags;
	// disabling IP limit because it will be used the same IP address to check they come from the same subnetwork.
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = false;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_subnetwork = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);
	ASSERT_FALSE (node->flags.disable_max_peers_per_subnetwork);

	const auto max_subnetwork_connections = node->config.network->max_peers_per_subnetwork;
	ASSERT_GE (max_subnetwork_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_subnetwork_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_subnetwork_connections + 1);

	for (auto idx = 0; idx < max_subnetwork_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_subnetwork_connections);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_subnetwork), 1);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_subnetwork_connections + 1);
}

TEST (tcp_listener, max_peers_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network->max_peers_per_ip;
	ASSERT_GE (max_ip_connections, 1);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), connect_handler);
		client_list.push_back (client);
	}

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), max_ip_connections + 1);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::max_per_ip), 0);
	ASSERT_TIMELY_EQ (5s, connection_attempts, max_ip_connections + 1);
}

/*
 * A connection that opens with a handshake query gets a handshake message back from the listening node
 */
TEST (tcp_listener, node_id_handshake)
{
	nano::test::system system (1);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*system.nodes[0]));
	auto bootstrap_endpoint (system.nodes[0]->tcp_listener.endpoint ());

	// Connect and send a query, any cookie will do
	auto const query = system.nodes[0]->network.prepare_handshake_query ();
	nano::messages::node_id_handshake node_id_handshake{ nano::dev::network_params.network, query };
	auto input (node_id_handshake.to_shared_const_buffer ());
	std::atomic<bool> write_done (false);
	socket->async_connect (bootstrap_endpoint, [&input, socket, &write_done] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		socket->async_write (input, [&input, &write_done] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
			ASSERT_EQ (input.size (), size_a);
			write_done = true;
		});
	});

	ASSERT_TIMELY (5s, write_done);

	// The node writes back a message the size of a handshake with a response in it
	nano::messages::node_id_handshake::response_payload response_zero{ 0 };
	nano::messages::node_id_handshake node_id_handshake_response{ nano::dev::network_params.network, std::nullopt, response_zero };
	auto output (node_id_handshake_response.to_bytes ());
	std::atomic<bool> done (false);
	socket->async_read (output, output->size (), [&output, &done] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
		ASSERT_EQ (output->size (), size_a);
		done = true;
	});
	ASSERT_TIMELY (5s, done);
}

TEST (tcp_listener, timeout_empty)
{
	nano::test::system system;
	nano::node_config config;
	config.tcp->handshake_timeout = 2s;
	auto node0 = system.add_node (config);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));
	socket->async_connect (node0->tcp_listener.endpoint (), [] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
	});
	ASSERT_TIMELY_EQ (5s, node0->tcp_listener.connection_count (), 1);
	ASSERT_TIMELY_EQ (10s, node0->tcp_listener.connection_count (), 0);
}

/*
 * A connection that sends its handshake query and then never answers the node's own is closed once the handshake timeout passes
 */
TEST (tcp_listener, timeout_node_id_handshake)
{
	nano::test::system system;
	nano::node_config config;
	config.tcp->handshake_timeout = 2s;
	auto node0 = system.add_node (config);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));

	// Connect and send a query, any cookie will do
	auto const query = node0->network.prepare_handshake_query ();
	nano::messages::node_id_handshake node_id_handshake{ nano::dev::network_params.network, query };
	nano::transport::peer_info const peer{ .protocol_version = node0->network_params.network.protocol_version };
	auto channel = std::make_shared<nano::transport::tcp_channel> (*node0, socket, peer);
	socket->async_connect (node0->tcp_listener.endpoint (), [&node_id_handshake, channel] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		channel->send (node_id_handshake, nano::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	});

	// The node takes the query, then drops the connection that leaves its handshake unfinished
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake) != 0);
	ASSERT_TIMELY_EQ (5s, node0->tcp_listener.connection_count (), 1);
	ASSERT_TIMELY_EQ (10s, node0->tcp_listener.connection_count (), 0);
}

// A header declaring the maximum payload its length field can encode must not crash the node.
TEST (tcp_listener, asc_pull_oversized_payload_no_crash)
{
	nano::test::system system;
	nano::node_config config;
	config.tcp->handshake_timeout = 2s;
	auto node = system.add_node (config);

	nano::messages::message_header header{ nano::dev::network_params.network, nano::messages::message_type::asc_pull_req };
	header.extensions = nano::messages::message_header::extensions_bitset_t{ 0xffff };

	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	{
		nano::vectorstream stream{ *bytes };
		header.serialize (stream);
	}

	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> write_done{ false };
	socket->async_connect (node->tcp_listener.endpoint (), [socket, bytes, &write_done] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		socket->async_write (bytes, [&write_done] (boost::system::error_code const & ec, size_t size) {
			ASSERT_FALSE (ec);
			write_done = true;
		});
	});
	ASSERT_TIMELY (5s, write_done);

	// The header fits the buffer, so the node reads it and stalls on the absent body until timeout.
	ASSERT_TIMELY_EQ (10s, node->tcp_listener.connection_count (), 0);
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

// Outbound connections must not consume the inbound connection budget
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

	// A listening socket that never accepts, the kernel completes the handshakes and keeps the outbound connections open
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor{ io_ctx, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } };
	auto const target = acceptor.local_endpoint ();

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
	std::vector<std::shared_ptr<nano::transport::tcp_socket>> clients;
	for (auto idx = 0; idx < 2; ++idx)
	{
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), [] (boost::system::error_code const & ec) {
			ASSERT_FALSE (ec);
		});
		clients.push_back (client);
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
		auto client = std::make_shared<nano::transport::tcp_socket> (*node);
		client->async_connect (node->network.endpoint (), [] (boost::system::error_code const & ec) {
			ASSERT_EQ (ec.value (), 0);
		});
		return client;
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
	std::deque<std::shared_ptr<nano::transport::tcp_socket>> clients;
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

	// A bare listening socket stands in for the peer, so no handshake or reachout adds connections of its own
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor{ io_ctx, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } };
	auto const target = acceptor.local_endpoint ();

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
