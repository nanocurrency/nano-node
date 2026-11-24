#include <nano/lib/files.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <vector>

using namespace std::chrono_literals;

/*
 * A connection that carries no traffic is closed once the silence timeout passes, and counted as a silent drop rather than an I/O timeout.
 * The test peer is not a node socket, so the node's side of the connection is the only socket counted
 */
TEST (socket_timeout, silence)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	// The pending handshake read must not time out first
	config.tcp->io_timeout = std::chrono::seconds::max ();
	config.tcp->silent_timeout = 2s;
	auto & node = *system.add_node (config);

	// Keep the node's socket for the connection about to be accepted
	std::promise<std::shared_ptr<nano::transport::tcp_socket>> accepted;
	node.tcp_listener.connection_accepted.add ([&accepted] (auto const & socket, auto const & server) {
		accepted.set_value (socket);
	});

	// A peer connects and stays silent
	nano::test::peer peer{ node };
	auto future = accepted.get_future ();
	ASSERT_EQ (std::future_status::ready, future.wait_for (5s));
	auto const socket = future.get ();

	// The node closes its side for the silence, not for a stalled read
	ASSERT_TIMELY (10s, !socket->alive ());
	ASSERT_TRUE (socket->has_timed_out ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_silence));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_silent_connection_drop, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
}

/*
 * A socket that is not connected by the connect timeout is closed and counted as a connect timeout.
 * The timeout runs from the socket's creation, so a socket that never starts connecting shows it without depending on any network
 */
TEST (socket_timeout, connect)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.tcp->connect_timeout = 1s;
	auto & node = *system.add_node (config);

	// A client socket that never connects
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node);

	// The checkup closes it once the connect timeout has passed
	ASSERT_TIMELY (5s, !socket->alive ());
	ASSERT_TRUE (socket->has_timed_out ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_connect));
}

/*
 * A read that gets no data within the I/O timeout fails, and the socket is closed and counted as an inbound I/O timeout
 */
TEST (socket_timeout, read)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	node.config.tcp->io_timeout = 2s;

	// A listener that holds the connection and never sends
	nano::test::peer_listener listener{ node };
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node);
	ASSERT_FALSE (socket->blocking_connect (listener.endpoint ()));

	// The read waits until the timeout closes the socket, and fails as timed out
	ASSERT_EQ (boost::asio::error::timed_out, socket->blocking_read (std::make_shared<std::vector<uint8_t>> (1), 1));
	ASSERT_TRUE (socket->has_timed_out ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_receive));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
}

/*
 * A write that cannot complete within the I/O timeout, because the peer reads nothing and the buffers on both sides are full, fails,
 * and the socket is closed and counted as an outbound I/O timeout
 */
TEST (socket_timeout, write)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	node.config.tcp->io_timeout = 2s;

	// A listener that holds the connection and never reads from it
	nano::test::peer_listener listener{ node };
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node);
	ASSERT_FALSE (socket->blocking_connect (listener.endpoint ()));

	// Writes succeed until the buffers fill, then one stalls until the timeout closes the socket
	auto const buffer = std::make_shared<std::vector<uint8_t>> (128 * 1024);
	boost::system::error_code ec;
	for (auto i = 0; i < 1024 && !ec; ++i)
	{
		ec = socket->blocking_write (buffer, buffer->size ());
	}

	ASSERT_EQ (boost::asio::error::timed_out, ec);
	ASSERT_TRUE (socket->has_timed_out ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_socket_timeout, nano::stat::detail::timeout_send));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}

/*
 * The I/O timeout applies to each read on its own: after a read that received data, the next one still times out when no more data comes
 */
TEST (socket_timeout, read_after_receive)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	node.config.tcp->io_timeout = 2s;

	// A peer that sends one byte and then nothing
	nano::test::peer_listener listener{ node };
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node);
	ASSERT_FALSE (socket->blocking_connect (listener.endpoint ()));
	auto const peer = listener.accept ();
	ASSERT_NE (nullptr, peer);
	std::vector<uint8_t> const byte (1);
	ASSERT_TRUE (peer->send_bytes (byte));

	// The first read gets the byte, the second waits until the timeout closes the socket
	auto const buffer = std::make_shared<std::vector<uint8_t>> (1);
	ASSERT_FALSE (socket->blocking_read (buffer, 1));
	ASSERT_TRUE (socket->blocking_read (buffer, 1));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_read_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::in));
}

/*
 * The I/O timeout applies to each write on its own: after a write that went through, a later one still times out once the buffers are full
 */
TEST (socket_timeout, write_after_send)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	node.config.tcp->io_timeout = 2s;

	// A listener that holds the connection and never reads from it
	nano::test::peer_listener listener{ node };
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node);
	ASSERT_FALSE (socket->blocking_connect (listener.endpoint ()));

	// The first write goes through, later ones fill the buffers until one stalls and the timeout closes the socket
	ASSERT_FALSE (socket->blocking_write (std::make_shared<std::vector<uint8_t>> (1), 1));
	auto const buffer = std::make_shared<std::vector<uint8_t>> (128 * 1024);
	boost::system::error_code ec;
	for (auto i = 0; i < 1024 && !ec; ++i)
	{
		ec = socket->blocking_write (buffer, buffer->size ());
	}

	ASSERT_TRUE (ec);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_write_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::tcp_io_timeout_drop, nano::stat::dir::out));
}
