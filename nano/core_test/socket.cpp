#include <boost/thread.hpp>
#include <gtest/gtest.h>
#include <nano/core_test/testutil.hpp>
#include <nano/node/socket.hpp>
#include <nano/node/testing.hpp>

using namespace std::chrono_literals;

TEST (socket, concurrent_writes)
{
	nano::inactive_node inactivenode;
	auto node = inactivenode.node;

	// This gives more realistic execution than using system#poll, allowing writes to
	// queue up and drain concurrently.
	nano::thread_runner runner (node->io_ctx, 1);

	// We're expecting 20 messages
	nano::util::counted_completion read_count_completion (20);
	std::function<void(std::shared_ptr<nano::socket>)> reader = [&read_count_completion, &reader](std::shared_ptr<nano::socket> socket_a) {
		auto buff (std::make_shared<std::vector<uint8_t>> ());
		buff->resize (1);
		socket_a->async_read (buff, 1, [&read_count_completion, &reader, socket_a, buff](boost::system::error_code const & ec, size_t size_a) {
			if (!ec)
			{
				if (read_count_completion.increment () < 20)
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
	auto server_socket (std::make_shared<nano::server_socket> (node, endpoint, 4, nano::socket::concurrency::multi_writer));
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

	constexpr size_t client_count = 5;
	nano::util::counted_completion connection_count_completion (client_count);
	std::vector<std::shared_ptr<nano::socket>> clients;
	for (unsigned i = 0; i < 5; i++)
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
	ASSERT_TRUE (connection_count_completion.await_count_for (10s));

	// Execute overlapping writes from multiple threads
	auto client (clients[0]);
	for (int i = 0; i < client_count; i++)
	{
		std::thread runner ([&client]() {
			for (int i = 0; i < 4; i++)
			{
				auto buff (std::make_shared<std::vector<uint8_t>> ());
				buff->push_back ('A' + i);
				client->async_write (buff);
			}
		});
		runner.detach ();
	}

	ASSERT_TRUE (read_count_completion.await_count_for (10s));
	node->stop ();
	runner.join (true);
}
