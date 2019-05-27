#include <nano/core_test/testutil.hpp>
#include <nano/node/socket.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

#include <boost/thread.hpp>

using namespace std::chrono_literals;

TEST (socket, concurrent_writes)
{
	nano::inactive_node inactivenode;
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
	std::function<void(std::shared_ptr<nano::socket>)> reader = [&read_count_completion, &total_message_count, &reader](std::shared_ptr<nano::socket> socket_a) {
		auto buff (std::make_shared<std::vector<uint8_t>> ());
		buff->resize (1);
		socket_a->async_read (buff, 1, [&read_count_completion, &reader, &total_message_count, socket_a, buff](boost::system::error_code const & ec, size_t size_a) {
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
	};

	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 25000);

	auto server_socket (std::make_shared<nano::server_socket> (node, endpoint, max_connections, nano::socket::concurrency::multi_writer));
	boost::system::error_code ec;
	server_socket->start (ec);
	ASSERT_FALSE (ec);
	std::vector<std::shared_ptr<nano::socket>> connections;

	// On every new connection, start reading data
	server_socket->on_connection ([&connections, &reader](std::shared_ptr<nano::socket> new_connection, boost::system::error_code const & ec_a) {
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
		auto client (std::make_shared<nano::socket> (node, boost::none, nano::socket::concurrency::multi_writer));
		clients.push_back (client);
		client->async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 25000),
		[&connection_count_completion](boost::system::error_code const & ec_a) {
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
		// Note: this gives a warning on most compilers because message_count is constexpr and a
		// capture isn't needed. However, removing it fails to compile on VS2017 due to a known compiler bug.
		client_threads.emplace_back ([&client, &message_count]() {
			for (int i = 0; i < message_count; i++)
			{
				auto buff (std::make_shared<std::vector<uint8_t>> ());
				buff->push_back ('A' + i);
				client->async_write (buff);
			}
		});
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
