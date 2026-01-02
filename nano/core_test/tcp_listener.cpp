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

TEST (tcp_listener, node_id_handshake)
{
	nano::test::system system (1);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*system.nodes[0]));
	auto bootstrap_endpoint (system.nodes[0]->tcp_listener.endpoint ());
	auto cookie (system.nodes[0]->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (bootstrap_endpoint)));
	ASSERT_TRUE (cookie);
	nano::messages::node_id_handshake::query_payload query{ *cookie };
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
	config.tcp.handshake_timeout = 2s;
	auto node0 = system.add_node (config);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));
	socket->async_connect (node0->tcp_listener.endpoint (), [] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
	});
	ASSERT_TIMELY_EQ (5s, node0->tcp_listener.connection_count (), 1);
	ASSERT_TIMELY_EQ (10s, node0->tcp_listener.connection_count (), 0);
}

TEST (tcp_listener, timeout_node_id_handshake)
{
	nano::test::system system;
	nano::node_config config;
	config.tcp.handshake_timeout = 2s;
	auto node0 = system.add_node (config);
	auto socket (std::make_shared<nano::transport::tcp_socket> (*node0));
	auto cookie (node0->network.syn_cookies.assign (nano::transport::map_tcp_to_endpoint (node0->tcp_listener.endpoint ())));
	ASSERT_TRUE (cookie);
	nano::messages::node_id_handshake::query_payload query{ *cookie };
	nano::messages::node_id_handshake node_id_handshake{ nano::dev::network_params.network, query };
	auto channel = std::make_shared<nano::transport::tcp_channel> (*node0, socket);
	socket->async_connect (node0->tcp_listener.endpoint (), [&node_id_handshake, channel] (boost::system::error_code const & ec) {
		ASSERT_FALSE (ec);
		channel->send (node_id_handshake, nano::transport::traffic_type::test, [] (boost::system::error_code const & ec, size_t size_a) {
			ASSERT_FALSE (ec);
		});
	});
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake) != 0);
	ASSERT_TIMELY_EQ (5s, node0->tcp_listener.connection_count (), 1);
	ASSERT_TIMELY_EQ (10s, node0->tcp_listener.connection_count (), 0);
}