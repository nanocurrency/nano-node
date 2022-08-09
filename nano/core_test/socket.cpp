#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/boost/asio/ip/network_v6.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/read.hpp>

#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

TEST (socket, max_connections)
{
	nano::test::system system;

	auto node = system.add_node ();

	auto server_port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint listen_endpoint{ boost::asio::ip::address_v6::any (), server_port };
	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), server_port };

	// start a server socket that allows max 2 live connections
	auto server_socket = std::make_shared<nano::server_socket> (*node, listen_endpoint, 2);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);

	// successful incoming connections are stored in server_sockets to keep them alive (server side)
	std::vector<std::shared_ptr<nano::socket>> server_sockets;
	server_socket->on_connection ([&server_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		server_sockets.push_back (new_connection);
		return true;
	});

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start 3 clients, 2 will persist but 1 will be dropped

	auto client1 = std::make_shared<nano::client_socket> (*node);
	client1->async_connect (dst_endpoint, connect_handler);

	auto client2 = std::make_shared<nano::client_socket> (*node);
	client2->async_connect (dst_endpoint, connect_handler);

	auto client3 = std::make_shared<nano::client_socket> (*node);
	client3->async_connect (dst_endpoint, connect_handler);

	auto get_tcp_accept_failures = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in);
	};

	auto get_tcp_accept_successes = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
	};

	ASSERT_TIMELY (5s, get_tcp_accept_failures () == 1);
	ASSERT_TIMELY (5s, get_tcp_accept_successes () == 2);
	ASSERT_TIMELY (5s, connection_attempts == 3);

	// create space for one socket and fill the connections table again

	server_sockets[0].reset ();

	auto client4 = std::make_shared<nano::client_socket> (*node);
	client4->async_connect (dst_endpoint, connect_handler);

	auto client5 = std::make_shared<nano::client_socket> (*node);
	client5->async_connect (dst_endpoint, connect_handler);

	ASSERT_TIMELY (5s, get_tcp_accept_failures () == 2);
	ASSERT_TIMELY (5s, get_tcp_accept_successes () == 3);
	ASSERT_TIMELY (5s, connection_attempts == 5);

	// close all existing sockets and fill the connections table again
	// start counting form 1 because 0 is the already closed socket

	server_sockets[1].reset ();
	server_sockets[2].reset ();
	ASSERT_EQ (server_sockets.size (), 3);

	auto client6 = std::make_shared<nano::client_socket> (*node);
	client6->async_connect (dst_endpoint, connect_handler);

	auto client7 = std::make_shared<nano::client_socket> (*node);
	client7->async_connect (dst_endpoint, connect_handler);

	auto client8 = std::make_shared<nano::client_socket> (*node);
	client8->async_connect (dst_endpoint, connect_handler);

	ASSERT_TIMELY (5s, get_tcp_accept_failures () == 3);
	ASSERT_TIMELY (5s, get_tcp_accept_successes () == 5);
	ASSERT_TIMELY (5s, connection_attempts == 8); // connections initiated by the client
	ASSERT_TIMELY (5s, server_sockets.size () == 5); // connections accepted by the server

	node->stop ();
}

TEST (socket, max_connections_per_ip)
{
	nano::test::system system;

	auto node = system.add_node ();
	ASSERT_FALSE (node->flags.disable_max_peers_per_ip);

	auto server_port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint listen_endpoint{ boost::asio::ip::address_v6::any (), server_port };
	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), server_port };

	const auto max_ip_connections = node->network_params.network.max_peers_per_ip;
	ASSERT_TRUE (max_ip_connections >= 1);

	const auto max_global_connections = 1000;

	auto server_socket = std::make_shared<nano::server_socket> (*node, listen_endpoint, max_global_connections);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);

	// successful incoming connections are stored in server_sockets to keep them alive (server side)
	std::vector<std::shared_ptr<nano::socket>> server_sockets;
	server_socket->on_connection ([&server_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		server_sockets.push_back (new_connection);
		return true;
	});

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::client_socket> (*node);
		client->async_connect (dst_endpoint, connect_handler);
		client_list.push_back (client);
	}

	auto get_tcp_max_per_ip = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::in);
	};

	auto get_tcp_accept_successes = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
	};

	ASSERT_TIMELY (5s, get_tcp_accept_successes () == max_ip_connections);
	ASSERT_TIMELY (5s, get_tcp_max_per_ip () == 1);
	ASSERT_TIMELY (5s, connection_attempts == max_ip_connections + 1);

	node->stop ();
}

TEST (socket, limited_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto network = nano::socket_functions::get_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713/32", network.to_string ());
	ASSERT_EQ ("a41d:b7b2::/32", network.canonical ().to_string ());
}

TEST (socket, first_ipv6_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto first_address = nano::socket_functions::first_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2::", first_address.to_string ());
}

TEST (socket, last_ipv6_subnet_address)
{
	auto address = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713");
	auto last_address = nano::socket_functions::last_ipv6_subnet_address (address.to_v6 (), 32); // network prefix = 32.
	ASSERT_EQ ("a41d:b7b2:ffff:ffff:ffff:ffff:ffff:ffff", last_address.to_string ());
}

TEST (socket, count_subnetwork_connections)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto address0 = boost::asio::ip::make_address ("a41d:b7b1:ffff:ffff:ffff:ffff:ffff:ffff"); // out of network prefix
	auto address1 = boost::asio::ip::make_address ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"); // referece address
	auto address2 = boost::asio::ip::make_address ("a41d:b7b2::"); // start of the network range
	auto address3 = boost::asio::ip::make_address ("a41d:b7b2::1");
	auto address4 = boost::asio::ip::make_address ("a41d:b7b2:ffff:ffff:ffff:ffff:ffff:ffff"); // end of the network range
	auto address5 = boost::asio::ip::make_address ("a41d:b7b3::"); // out of the network prefix
	auto address6 = boost::asio::ip::make_address ("a41d:b7b3::1"); // out of the network prefix

	auto connection0 = std::make_shared<nano::client_socket> (*node);
	auto connection1 = std::make_shared<nano::client_socket> (*node);
	auto connection2 = std::make_shared<nano::client_socket> (*node);
	auto connection3 = std::make_shared<nano::client_socket> (*node);
	auto connection4 = std::make_shared<nano::client_socket> (*node);
	auto connection5 = std::make_shared<nano::client_socket> (*node);
	auto connection6 = std::make_shared<nano::client_socket> (*node);

	nano::address_socket_mmap connections_per_address;
	connections_per_address.emplace (address0, connection0);
	connections_per_address.emplace (address1, connection1);
	connections_per_address.emplace (address2, connection2);
	connections_per_address.emplace (address3, connection3);
	connections_per_address.emplace (address4, connection4);
	connections_per_address.emplace (address5, connection5);
	connections_per_address.emplace (address6, connection6);

	// Asserts it counts only the connections for the specified address and its network prefix.
	ASSERT_EQ (4, nano::socket_functions::count_subnetwork_connections (connections_per_address, address1.to_v6 (), 32));
}

TEST (socket, max_connections_per_subnetwork)
{
	nano::test::system system;

	nano::node_flags node_flags;
	// disabling IP limit because it will be used the same IP address to check they come from the same subnetwork.
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = false;
	auto node = system.add_node (node_flags);
	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);
	ASSERT_FALSE (node->flags.disable_max_peers_per_subnetwork);

	auto server_port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint listen_endpoint{ boost::asio::ip::address_v6::any (), server_port };
	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), server_port };

	const auto max_subnetwork_connections = node->network_params.network.max_peers_per_subnetwork;
	ASSERT_TRUE (max_subnetwork_connections >= 1);

	const auto max_global_connections = 1000;

	auto server_socket = std::make_shared<nano::server_socket> (*node, listen_endpoint, max_global_connections);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);

	// successful incoming connections are stored in server_sockets to keep them alive (server side)
	std::vector<std::shared_ptr<nano::socket>> server_sockets;
	server_socket->on_connection ([&server_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		server_sockets.push_back (new_connection);
		return true;
	});

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_subnetwork_connections
	std::vector<std::shared_ptr<nano::socket>> client_list;
	client_list.reserve (max_subnetwork_connections + 1);

	for (auto idx = 0; idx < max_subnetwork_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::client_socket> (*node);
		client->async_connect (dst_endpoint, connect_handler);
		client_list.push_back (client);
	}

	auto get_tcp_max_per_subnetwork = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_subnetwork, nano::stat::dir::in);
	};

	auto get_tcp_accept_successes = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
	};

	ASSERT_TIMELY (5s, get_tcp_accept_successes () == max_subnetwork_connections);
	ASSERT_TIMELY (5s, get_tcp_max_per_subnetwork () == 1);
	ASSERT_TIMELY (5s, connection_attempts == max_subnetwork_connections + 1);

	node->stop ();
}

TEST (socket, disabled_max_peers_per_ip)
{
	nano::test::system system;

	nano::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	auto node = system.add_node (node_flags);
	ASSERT_TRUE (node->flags.disable_max_peers_per_ip);

	auto server_port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint listen_endpoint{ boost::asio::ip::address_v6::any (), server_port };
	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), server_port };

	const auto max_ip_connections = node->network_params.network.max_peers_per_ip;
	ASSERT_TRUE (max_ip_connections >= 1);

	const auto max_global_connections = 1000;

	auto server_socket = std::make_shared<nano::server_socket> (*node, listen_endpoint, max_global_connections);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);

	// successful incoming connections are stored in server_sockets to keep them alive (server side)
	std::vector<std::shared_ptr<nano::socket>> server_sockets;
	server_socket->on_connection ([&server_sockets] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		server_sockets.push_back (new_connection);
		return true;
	});

	// client side connection tracking
	std::atomic<size_t> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		++connection_attempts;
	};

	// start n clients, n-1 will persist but 1 will be dropped, where n == max_ip_connections
	std::vector<std::shared_ptr<nano::socket>> client_list;
	client_list.reserve (max_ip_connections + 1);

	for (auto idx = 0; idx < max_ip_connections + 1; ++idx)
	{
		auto client = std::make_shared<nano::client_socket> (*node);
		client->async_connect (dst_endpoint, connect_handler);
		client_list.push_back (client);
	}

	auto get_tcp_max_per_ip = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_max_per_ip, nano::stat::dir::in);
	};

	auto get_tcp_accept_successes = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in);
	};

	ASSERT_TIMELY (5s, get_tcp_accept_successes () == max_ip_connections + 1);
	ASSERT_TIMELY (5s, get_tcp_max_per_ip () == 0);
	ASSERT_TIMELY (5s, connection_attempts == max_ip_connections + 1);

	node->stop ();
}

TEST (socket, disconnection_of_silent_connections)
{
	nano::test::system system;

	nano::node_config config;
	// Increasing the timer timeout, so we don't let the connection to timeout due to the timer checker.
	config.tcp_io_timeout = std::chrono::seconds::max ();
	config.network_params.network.idle_timeout = std::chrono::seconds::max ();
	// Silent connections are connections open by external peers that don't contribute with any data.
	config.network_params.network.silent_connection_tolerance_time = std::chrono::seconds{ 5 };

	auto node = system.add_node (config);

	auto server_port = nano::test::get_available_port ();
	boost::asio::ip::tcp::endpoint listen_endpoint{ boost::asio::ip::address_v6::any (), server_port };
	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), server_port };

	// start a server listening socket
	auto server_socket = std::make_shared<nano::server_socket> (*node, listen_endpoint, 1);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);

	// on a connection, a server data socket is created. The shared pointer guarantees the object's lifecycle until the end of this test.
	std::shared_ptr<nano::socket> server_data_socket;
	server_socket->on_connection ([&server_data_socket] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		server_data_socket = new_connection;
		return true;
	});

	// Instantiates a client to simulate an incoming connection.
	auto client_socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> connected{ false };
	// Opening a connection that will be closed because it remains silent during the tolerance time.
	client_socket->async_connect (dst_endpoint, [client_socket, &connected] (boost::system::error_code const & ec_a) {
		ASSERT_FALSE (ec_a);
		connected = true;
	});
	ASSERT_TIMELY (4s, connected);
	// Checking the connection was closed.
	ASSERT_TIMELY (10s, server_data_socket != nullptr);
	ASSERT_TIMELY (10s, server_data_socket->is_closed ());

	auto get_tcp_io_timeout_drops = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in);
	};
	auto get_tcp_silent_connection_drops = [&node] () {
		return node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in);
	};
	// Just to ensure the disconnection wasn't due to the timer timeout.
	ASSERT_EQ (0, get_tcp_io_timeout_drops ());
	// Asserts the silent checker worked.
	ASSERT_EQ (1, get_tcp_silent_connection_drops ());

	node->stop ();
}

TEST (socket, drop_policy)
{
	auto node_flags = nano::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	nano::inactive_node inactivenode (nano::unique_path (), node_flags);
	auto node = inactivenode.node;

	nano::thread_runner runner (node->io_ctx, 1);

	std::vector<std::shared_ptr<nano::socket>> connections;

	auto func = [&] (size_t total_message_count, nano::buffer_drop_policy drop_policy) {
		auto server_port (nano::test::get_available_port ());
		boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::any (), server_port);

		auto server_socket = std::make_shared<nano::server_socket> (*node, endpoint, 1);
		boost::system::error_code ec;
		server_socket->start (ec);
		ASSERT_FALSE (ec);

		// Accept connection, but don't read so the writer will drop.
		server_socket->on_connection ([&connections] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
			connections.push_back (new_connection);
			return true;
		});

		auto client = std::make_shared<nano::client_socket> (*node);
		nano::transport::channel_tcp channel{ *node, client };
		nano::util::counted_completion write_completion (static_cast<unsigned> (total_message_count));

		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), server_socket->listening_port ()),
		[&channel, total_message_count, node, &write_completion, &drop_policy, client] (boost::system::error_code const & ec_a) mutable {
			for (int i = 0; i < total_message_count; i++)
			{
				std::vector<uint8_t> buff (1);
				channel.send_buffer (
				nano::shared_const_buffer (std::move (buff)), [&write_completion, client] (boost::system::error_code const & ec, size_t size_a) mutable {
					client.reset ();
					write_completion.increment ();
				},
				drop_policy);
			}
		});
		ASSERT_FALSE (write_completion.await_count_for (std::chrono::seconds (5)));
		ASSERT_EQ (1, client.use_count ());
	};

	// We're going to write twice the queue size + 1, and the server isn't reading
	// The total number of drops should thus be 1 (the socket allows doubling the queue size for no_socket_drop)
	func (nano::socket::queue_size_max * 2 + 1, nano::buffer_drop_policy::no_socket_drop);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_no_socket_drop, nano::stat::dir::out));
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_drop, nano::stat::dir::out));

	func (nano::socket::queue_size_max + 1, nano::buffer_drop_policy::limiter);
	// The stats are accumulated from before
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_no_socket_drop, nano::stat::dir::out));
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_drop, nano::stat::dir::out));

	node->stop ();
	runner.stop_event_processing ();
	runner.join ();
}

TEST (socket, concurrent_writes)
{
	auto node_flags = nano::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	node_flags.disable_max_peers_per_ip = true;
	node_flags.disable_max_peers_per_subnetwork = true;
	nano::inactive_node inactivenode (nano::unique_path (), node_flags);
	auto node = inactivenode.node;

	// This gives more realistic execution than using system#poll, allowing writes to
	// queue up and drain concurrently.
	nano::thread_runner runner (node->io_ctx, 1);

	constexpr size_t max_connections = 4;
	constexpr size_t client_count = max_connections;
	constexpr size_t message_count = 4;
	constexpr size_t total_message_count = client_count * message_count;

	// We're expecting client_count*4 messages
	nano::util::counted_completion read_count_completion (total_message_count);
	std::function<void (std::shared_ptr<nano::socket> const &)> reader = [&read_count_completion, &total_message_count, &reader] (std::shared_ptr<nano::socket> const & socket_a) {
		auto buff (std::make_shared<std::vector<uint8_t>> ());
		buff->resize (1);
#ifndef _WIN32
#pragma GCC diagnostic push
#if defined(__has_warning)
#if __has_warning("-Wunused-lambda-capture")
/** total_message_count is constexpr and a capture isn't needed. However, removing it fails to compile on VS2017 due to a known compiler bug. */
#pragma GCC diagnostic ignored "-Wunused-lambda-capture"
#endif
#endif
#endif
		socket_a->async_read (buff, 1, [&read_count_completion, &reader, &total_message_count, socket_a, buff] (boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				if (read_count_completion.increment () < total_message_count)
				{
					reader (socket_a);
				}
			}
			else if (ec != boost::asio::error::eof)
			{
				std::cerr << "async_read: " << ec.message () << std::endl;
			}
		});
#ifndef _WIN32
#pragma GCC diagnostic pop
#endif
	};

	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 25000);

	auto server_socket = std::make_shared<nano::server_socket> (*node, endpoint, max_connections);
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);
	std::vector<std::shared_ptr<nano::socket>> connections;

	// On every new connection, start reading data
	server_socket->on_connection ([&connections, &reader] (std::shared_ptr<nano::socket> const & new_connection, boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			std::cerr << "on_connection: " << ec_a.message () << std::endl;
		}
		else
		{
			connections.push_back (new_connection);
			reader (new_connection);
		}
		// Keep accepting connections
		return true;
	});

	nano::util::counted_completion connection_count_completion (client_count);
	std::vector<std::shared_ptr<nano::socket>> clients;
	for (unsigned i = 0; i < client_count; i++)
	{
		auto client = std::make_shared<nano::client_socket> (*node);
		clients.push_back (client);
		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 25000),
		[&connection_count_completion] (boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				std::cerr << "async_connect: " << ec_a.message () << std::endl;
			}
			else
			{
				connection_count_completion.increment ();
			}
		});
	}
	ASSERT_FALSE (connection_count_completion.await_count_for (10s));

	// Execute overlapping writes from multiple threads
	auto client (clients[0]);
	std::vector<std::thread> client_threads;
	for (int i = 0; i < client_count; i++)
	{
#ifndef _WIN32
#pragma GCC diagnostic push
#if defined(__has_warning)
#if __has_warning("-Wunused-lambda-capture")
/** total_message_count is constexpr and a capture isn't needed. However, removing it fails to compile on VS2017 due to a known compiler bug. */
#pragma GCC diagnostic ignored "-Wunused-lambda-capture"
#endif
#endif
#endif
		client_threads.emplace_back ([&client, &message_count] () {
			for (int i = 0; i < message_count; i++)
			{
				std::vector<uint8_t> buff;
				buff.push_back ('A' + i);
				client->async_write (nano::shared_const_buffer (std::move (buff)));
			}
		});
#ifndef _WIN32
#pragma GCC diagnostic pop
#endif
	}

	ASSERT_FALSE (read_count_completion.await_count_for (10s));
	node->stop ();
	runner.stop_event_processing ();
	runner.join ();

	ASSERT_EQ (node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_success, nano::stat::dir::in), client_count);
	// We may exhaust max connections and have some tcp accept failures, but no more than the client count
	ASSERT_LT (node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_accept_failure, nano::stat::dir::in), client_count);

	for (auto & t : client_threads)
	{
		t.join ();
	}
}

/**
 * Check that the socket correctly handles a tcp_io_timeout during tcp connect
 * Steps:
 *   set timeout to one second
 *   do a tcp connect that will block for at least a few seconds at the tcp level
 *   check that the connect returns error and that the correct counters have been incremented
 */
TEST (socket_timeout, connect)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (1);

	// try to connect to an IP address that most likely does not exist and will not reply
	// we want the tcp stack to not receive a negative reply, we want it to see silence and to keep trying
	// I use the un-routable IP address 10.255.254.253, which is likely to not exist
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::make_address_v6 ("::ffff:10.255.254.253"), nano::test::get_available_port ());

	// create a client socket and try to connect to the IP address that wil not respond
	auto socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&ec, &done] (boost::system::error_code const & ec_a) {
		if (ec_a)
		{
			ec = ec_a;
			done = true;
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY (6s, done == true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_connect_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, read)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), nano::test::get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not send any data
	boost::asio::ip::tcp::socket newsock (system.io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
	});

	// create a client socket to connect and call async_read, which should time out
	auto socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		socket->async_read (buffer, 1, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
			if (ec_a)
			{
				ec = ec_a;
				done = true;
			}
		});
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY (10s, done == true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, write)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), nano::test::get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not receive any data
	boost::asio::ip::tcp::socket newsock (system.io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		auto buffer = std::make_shared<std::vector<uint8_t>> (128 * 1024);
		for (auto i = 0; i < 1024; ++i)
		{
			socket->async_write (nano::shared_const_buffer{ buffer }, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
				if (ec_a)
				{
					ec = ec_a;
					done = true;
				}
			});
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY (10s, done == true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, read_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), nano::test::get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and send one byte only
	boost::asio::ip::tcp::socket newsock (system.io_ctx);
	acceptor.async_accept (newsock, [&newsock] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		nano::async_write (newsock, nano::shared_const_buffer (buffer), [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (!ec_a);
			debug_assert (size_a == 1);
		});
	});

	// create a client socket to connect and call async_read twice, the second call should time out
	auto socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		auto buffer = std::make_shared<std::vector<uint8_t>> (1);

		socket->async_read (buffer, 1, [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});

		socket->async_read (buffer, 1, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 0);
			if (ec_a)
			{
				ec = ec_a;
				done = true;
			}
		});
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY (10s, done == true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, write_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp_io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), nano::test::get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and read 2 bytes only
	boost::asio::ip::tcp::socket newsock (system.io_ctx);
	auto buffer = std::make_shared<std::vector<uint8_t>> (1);
	acceptor.async_accept (newsock, [&newsock, &buffer] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		boost::asio::async_read (newsock, boost::asio::buffer (buffer->data (), buffer->size ()), [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::client_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		debug_assert (!ec_a);
		auto buffer1 = std::make_shared<std::vector<uint8_t>> (1);
		auto buffer2 = std::make_shared<std::vector<uint8_t>> (128 * 1024);
		socket->async_write (nano::shared_const_buffer{ buffer1 }, [] (boost::system::error_code const & ec_a, size_t size_a) {
			debug_assert (size_a == 1);
		});
		for (auto i = 0; i < 1024; ++i)
		{
			socket->async_write (nano::shared_const_buffer{ buffer2 }, [&ec, &done] (boost::system::error_code const & ec_a, size_t size_a) {
				if (ec_a)
				{
					ec = ec_a;
					done = true;
				}
			});
		}
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY (10s, done == true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error, nano::stat::dir::in));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}
