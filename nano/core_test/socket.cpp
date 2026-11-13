#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/read.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

TEST (socket, disconnection_of_silent_connections)
{
	nano::test::system system;

	nano::node_config config;
	// Increasing the timer timeout, so we don't let the connection to timeout due to the timer checker.
	config.tcp->io_timeout = std::chrono::seconds::max ();
	// Silent connections are connections open by external peers that don't contribute with any data.
	config.tcp->silent_timeout = std::chrono::seconds{ 5 };
	auto node = system.add_node (config);

	// On a connection, a server data socket is created. The shared pointer guarantees the object's lifecycle until the end of this test.
	std::promise<std::shared_ptr<nano::transport::tcp_socket>> server_data_socket_promise;
	std::future<std::shared_ptr<nano::transport::tcp_socket>> server_data_socket_future = server_data_socket_promise.get_future ();
	node->tcp_listener.connection_accepted.add ([&server_data_socket_promise] (auto const & socket, auto const & server) {
		server_data_socket_promise.set_value (socket);
	});

	boost::asio::ip::tcp::endpoint dst_endpoint{ boost::asio::ip::address_v6::loopback (), node->tcp_listener.endpoint ().port () };

	// Instantiates a client to simulate an incoming connection.
	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> connected{ false };
	// Opening a connection that will be closed because it remains silent during the tolerance time.
	client_socket->async_connect (dst_endpoint, [client_socket, &connected] (boost::system::error_code const & ec_a) {
		ASSERT_FALSE (ec_a);
		connected = true;
	});
	ASSERT_TIMELY (5s, connected);

	// Checking the connection was closed.
	ASSERT_TIMELY (10s, server_data_socket_future.wait_for (0s) == std::future_status::ready);
	auto server_data_socket = server_data_socket_future.get ();
	ASSERT_TIMELY (10s, !server_data_socket->alive ());

	// Just to ensure the disconnection wasn't due to the timer timeout.
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
	// Asserts the silent checker worked.
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in));
}

/**
 * Check that the socket correctly handles a tcp_io_timeout during tcp connect
 * Steps:
 *   set timeout to one second
 *   do a tcp connect that will block for at least a few seconds at the tcp level
 *   check that the connect returns error and that the correct counters have been incremented
 *
 *   NOTE: it is possible that the O/S has tried to access the IP address 10.255.254.253 before
 *   and has it marked in the routing table as unroutable. In that case this test case will fail.
 *   If this test is run repeatedly the tests fails for this reason because the connection fails
 *   with "No route to host" error instead of a timeout.
 */
TEST (socket_timeout, connect)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp->io_timeout = 1s;

	// try to connect to an IP address that most likely does not exist and will not reply
	// we want the tcp stack to not receive a negative reply, we want it to see silence and to keep trying
	// I use the un-routable IP address 10.255.254.253, which is likely to not exist
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::make_address_v6 ("::ffff:10.255.254.253"), 1234);

	// create a client socket and try to connect to the IP address that will not respond
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (endpoint, [&ec, &done] (boost::system::error_code const & ec_a) {
		ec = ec_a;
		done = true;
	});

	// Sometimes the connect will be aborted but there will be no error, just check that the callback was called due to the timeout
	ASSERT_TIMELY_EQ (6s, done, true);
	ASSERT_TRUE (socket->has_timed_out ());
}

TEST (socket_timeout, read)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp->io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (*system.io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not send any data
	boost::asio::ip::tcp::socket newsock (*system.io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);
	});

	// create a client socket to connect and call async_read, which should time out
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);
	std::atomic<bool> done = false;
	boost::system::error_code ec;
	socket->async_connect (acceptor.local_endpoint (), [&socket, &ec, &done] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		socket->async_read (buffer, 1, [&ec, &done] (boost::system::error_code const & ec_a) {
			if (ec_a)
			{
				ec = ec_a;
				done = true;
			}
		});
	});

	// check that the callback was called and we got an error
	ASSERT_TIMELY_EQ (10s, done, true);
	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
}

TEST (socket_timeout, write)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp->io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (node->io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and create a newsock and do not receive any data
	boost::asio::ip::tcp::socket newsock (node->io_ctx);
	acceptor.async_accept (newsock, [] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node, nano::transport::socket_endpoint::client); // socket with a max queue size much larger than OS buffers

	auto ec_0 = socket->blocking_connect (acceptor.local_endpoint ());
	ASSERT_FALSE (ec_0);

	auto buffer = std::make_shared<std::vector<uint8_t>> (128 * 1024);

	// At some point the write should fail with a timeout
	boost::system::error_code ec;
	for (auto i = 0; i < 1024; ++i)
	{
		auto ec_1 = socket->blocking_write (buffer, buffer->size ());
		if (ec_1)
		{
			ec = ec_1;
			break;
		}
	}

	// check that the callback was called and we got an error
	ASSERT_TRUE (ec);
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

TEST (socket_timeout, read_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp->io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (node->io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and send one byte only
	boost::asio::ip::tcp::socket newsock (node->io_ctx);
	acceptor.async_accept (newsock, [&newsock] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		auto buffer = std::make_shared<std::vector<uint8_t>> (1);
		nano::async_write (newsock, nano::shared_const_buffer (buffer), [] (boost::system::error_code const & ec_a, size_t size_a) {
			EXPECT_TRUE (!ec_a);
			EXPECT_EQ (size_a, 1);
		});
	});

	// create a client socket to connect and call async_read twice, the second call should time out
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node);

	auto ec_0 = socket->blocking_connect (acceptor.local_endpoint ());
	ASSERT_FALSE (ec_0);

	auto buffer = std::make_shared<std::vector<uint8_t>> (1);

	auto ec_1 = socket->blocking_read (buffer, 1);
	ASSERT_FALSE (ec_1);

	auto ec_2 = socket->blocking_read (buffer, 1);
	ASSERT_TRUE (ec_2);

	// check that the callback was called and we got an error
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
}

TEST (socket_timeout, write_overlapped)
{
	// create one node and set timeout to 1 second
	nano::test::system system (1);
	std::shared_ptr<nano::node> node = system.nodes[0];
	node->config.tcp->io_timeout = std::chrono::seconds (2);

	// create a server socket
	boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v6::loopback (), system.get_available_port ());
	boost::asio::ip::tcp::acceptor acceptor (node->io_ctx);
	acceptor.open (endpoint.protocol ());
	acceptor.bind (endpoint);
	acceptor.listen (boost::asio::socket_base::max_listen_connections);

	// asynchronously accept an incoming connection and read 2 bytes only
	boost::asio::ip::tcp::socket newsock (node->io_ctx);
	auto buffer = std::make_shared<std::vector<uint8_t>> (1);
	acceptor.async_accept (newsock, [&newsock, &buffer] (boost::system::error_code const & ec_a) {
		EXPECT_FALSE (ec_a);

		boost::asio::async_read (newsock, boost::asio::buffer (buffer->data (), buffer->size ()), [] (boost::system::error_code const & ec_a, size_t size_a) {
			EXPECT_FALSE (ec_a);
			EXPECT_EQ (size_a, 1);
		});
	});

	// create a client socket and send lots of data to fill the socket queue on the local and remote side
	// eventually, the all tcp queues should fill up and async_write will not be able to progress
	// and the timeout should kick in and close the socket, which will cause the async_write to return an error
	auto socket = std::make_shared<nano::transport::tcp_socket> (*node, nano::transport::socket_endpoint::client);

	auto ec_0 = socket->blocking_connect (acceptor.local_endpoint ());
	ASSERT_FALSE (ec_0);

	auto buffer1 = std::make_shared<std::vector<uint8_t>> (1);
	auto buffer2 = std::make_shared<std::vector<uint8_t>> (128 * 1024);

	auto ec_1 = socket->blocking_write (nano::shared_const_buffer{ buffer1 }, 1);
	ASSERT_FALSE (ec_1);

	// At some point the write should fail with a timeout
	boost::system::error_code ec;
	for (auto i = 0; i < 1000; ++i)
	{
		auto ec_2 = socket->blocking_write (nano::shared_const_buffer{ buffer2 }, 128 * 1024);
		if (ec_2)
		{
			ec = ec_2;
			break;
		}
	}

	// check that the callback was called and we got an error
	ASSERT_TRUE (ec);
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error));

	// check that the socket was closed due to tcp_io_timeout timeout
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}
