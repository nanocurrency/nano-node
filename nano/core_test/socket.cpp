#include <nano/lib/threading.hpp>
#include <nano/node/socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (socket, max_connections)
{
	// this is here just so that ASSERT_TIMELY can be used
	nano::system system;

	auto node_flags = nano::inactive_node_flag_defaults ();
	node_flags.read_only = false;
	nano::inactive_node inactivenode (nano::unique_path (), node_flags);
	auto node = inactivenode.node;

	nano::thread_runner runner (node->io_ctx, 1);

	auto server_port (nano::get_available_port ());
	boost::asio::ip::tcp::endpoint listen_endpoint (boost::asio::ip::address_v6::any (), server_port);
	boost::asio::ip::tcp::endpoint dst_endpoint (boost::asio::ip::address_v6::loopback (), server_port);

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
	std::atomic<int> connection_attempts = 0;
	auto connect_handler = [&connection_attempts] (boost::system::error_code const & ec_a) {
		ASSERT_EQ (ec_a.value (), 0);
		connection_attempts++;
	};

	// start 3 clients, 2 will persist but 1 will be dropped

	auto client1 = std::make_shared<nano::socket> (*node, boost::none);
	client1->async_connect (dst_endpoint, connect_handler);

	auto client2 = std::make_shared<nano::socket> (*node, boost::none);
	client2->async_connect (dst_endpoint, connect_handler);

	auto client3 = std::make_shared<nano::socket> (*node, boost::none);
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

	auto client4 = std::make_shared<nano::socket> (*node, boost::none);
	client4->async_connect (dst_endpoint, connect_handler);

	auto client5 = std::make_shared<nano::socket> (*node, boost::none);
	client5->async_connect (dst_endpoint, connect_handler);

	ASSERT_TIMELY (5s, get_tcp_accept_failures () == 2);
	ASSERT_TIMELY (5s, get_tcp_accept_successes () == 3);
	ASSERT_TIMELY (5s, connection_attempts == 5);

	// close all existing sockets and fill the connections table again
	// start counting form 1 because 0 is the already closed socket

	server_sockets[1].reset ();
	server_sockets[2].reset ();
	ASSERT_EQ (server_sockets.size (), 3);

	auto client6 = std::make_shared<nano::socket> (*node, boost::none);
	client6->async_connect (dst_endpoint, connect_handler);

	auto client7 = std::make_shared<nano::socket> (*node, boost::none);
	client7->async_connect (dst_endpoint, connect_handler);

	auto client8 = std::make_shared<nano::socket> (*node, boost::none);
	client8->async_connect (dst_endpoint, connect_handler);

	ASSERT_TIMELY (5s, get_tcp_accept_failures () == 3);
	ASSERT_TIMELY (5s, get_tcp_accept_successes () == 5);
	ASSERT_TIMELY (5s, connection_attempts == 8); // connections initiated by the client
	ASSERT_TIMELY (5s, server_sockets.size () == 5); // connections accepted by the server

	node->stop ();
	runner.stop_event_processing ();
	runner.join ();
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
		auto server_port (nano::get_available_port ());
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

		auto client = std::make_shared<nano::socket> (*node, boost::none);
		nano::transport::channel_tcp channel{ *node, client };
		nano::util::counted_completion write_completion (static_cast<unsigned> (total_message_count));

		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v6::loopback (), server_port),
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
		auto client = std::make_shared<nano::socket> (*node, boost::none);
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
