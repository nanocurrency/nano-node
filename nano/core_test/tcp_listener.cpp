#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/boost/asio/ip/network_v6.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

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
	node_config.tcp.max_inbound_connections = 2;
	auto node = system.add_node (node_config, node_flags);

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start 3 clients, 2 will persist but 1 will be dropped
	auto client1 = std::make_shared<nano::transport::tcp_socket> (*node);
	client1->async_connect (node->network.endpoint (), connect_handler);

	auto client2 = std::make_shared<nano::transport::tcp_socket> (*node);
	client2->async_connect (node->network.endpoint (), connect_handler);

	auto client3 = std::make_shared<nano::transport::tcp_socket> (*node);
	client3->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 2);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 3);

	// create space for one socket and fill the connections table again
	{
		auto sockets1 = node->tcp_listener.sockets ();
		ASSERT_EQ (sockets1.size (), 2);
		sockets1[0]->close ();
	}
	ASSERT_TIMELY_EQ (10s, node->tcp_listener.sockets ().size (), 1);

	auto client4 = std::make_shared<nano::transport::tcp_socket> (*node);
	client4->async_connect (node->network.endpoint (), connect_handler);

	auto client5 = std::make_shared<nano::transport::tcp_socket> (*node);
	client5->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 3);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 3);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 5);

	// close all existing sockets and fill the connections table again
	{
		auto sockets2 = node->tcp_listener.sockets ();
		ASSERT_EQ (sockets2.size (), 2);
		sockets2[0]->close ();
		sockets2[1]->close ();
	}
	ASSERT_TIMELY_EQ (10s, node->tcp_listener.sockets ().size (), 0);

	auto client6 = std::make_shared<nano::transport::tcp_socket> (*node);
	client6->async_connect (node->network.endpoint (), connect_handler);

	auto client7 = std::make_shared<nano::transport::tcp_socket> (*node);
	client7->async_connect (node->network.endpoint (), connect_handler);

	auto client8 = std::make_shared<nano::transport::tcp_socket> (*node);
	client8->async_connect (node->network.endpoint (), connect_handler);

	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 5);
	ASSERT_ALWAYS_EQ (1s, node->stats.count (nano::stat::type::tcp_listener, nano::stat::detail::accept_success), 5);
	ASSERT_TIMELY_EQ (5s, connection_attempts, 8); // connections initiated by the client
}

TEST (tcp_listener, max_connections_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	nano::node_config node_config = system.default_config ();
	node_config.network.max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);
	ASSERT_FALSE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network.max_peers_per_ip;
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
	node_config.network.max_peers_per_subnetwork = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);
	ASSERT_FALSE (node->flags.disable_max_peers_per_subnetwork);

	const auto max_subnetwork_connections = node->config.network.max_peers_per_subnetwork;
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
	node_config.network.max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);

	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);

	auto server_port = system.get_available_port ();

	const auto max_ip_connections = node->config.network.max_peers_per_ip;
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

TEST (tcp_listener, tcp_node_id_handshake)
{
	nano::test::system system (1);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*system.nodes[0]));
	auto bootstrap_endpoint (system.nodes[0]->tcp_listener.endpoint ());
	auto cookie (system.nodes[0]->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (bootstrap_endpoint)));
	ASSERT_TRUE (cookie);
	nano::node_id_handshake::query_payload query{ *cookie };
	nano::node_id_handshake node_id_handshake{ nano::dev::network_params.network, query };
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

	nano::node_id_handshake::response_payload response_zero{ 0 };
	nano::node_id_handshake node_id_handshake_response{ nano::dev::network_params.network, std::nullopt, response_zero };
	auto output (node_id_handshake_response.to_bytes ());
	std::atomic<bool> done (false);
	socket->async_read (output, output->size (), [&output, &done] (boost::system::error_code const & ec, size_t size_a) {
		ASSERT_FALSE (ec);
		ASSERT_EQ (output->size (), size_a);
		done = true;
	});
	ASSERT_TIMELY (5s, done);
}

// Test disabled because it's failing intermittently.
// PR in which it got disabled: https://github.com/nanocurrency/nano-node/pull/3611
// Issue for investigating it: https://github.com/nanocurrency/nano-node/issues/3615
TEST (tcp_listener, DISABLED_tcp_listener_timeout_empty)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));
	std::atomic<bool> connected (false);
	socket->async_connect (node0->tcp_listener.endpoint (), [&connected] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		connected = true;
	});
	ASSERT_TIMELY (5s, connected);
	bool disconnected (false);
	system.deadline_set (std::chrono::seconds (6));
	while (!disconnected)
	{
		disconnected = node0->tcp_listener.connection_count () == 0;
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (tcp_listener, tcp_listener_timeout_node_id_handshake)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));
	auto cookie (node0->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (node0->tcp_listener.endpoint ())));
	ASSERT_TRUE (cookie);
	nano::node_id_handshake::query_payload query{ *cookie };
	nano::node_id_handshake node_id_handshake{ nano::dev::network_params.network, query };
	auto channel = std::make_shared<nano::transport::tcp_channel> (*node0, socket);
	socket->async_connect (node0->tcp_listener.endpoint (), [&node_id_handshake, channel] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		channel->send (node_id_handshake, nano::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	});
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake) != 0);
	ASSERT_EQ (node0->tcp_listener.connection_count (), 1);
	bool disconnected (false);
	system.deadline_set (std::chrono::seconds (20));
	while (!disconnected)
	{
		disconnected = node0->tcp_listener.connection_count () == 0;
		ASSERT_NO_ERROR (system.poll ());
	}
}