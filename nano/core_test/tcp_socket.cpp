#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/network_types.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>

#include <memory>

using namespace std::chrono_literals;

/*
 * A socket built over a connection that its peer has already reset still reports that peer's endpoint.
 * After a reset the endpoint can no longer be read from the connection, so the socket has to keep the one it was given
 */
TEST (tcp_socket, remote_endpoint_after_reset)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// A plain connection over loopback, `accepted` being the end the socket is built over
	boost::asio::ip::tcp::acceptor acceptor{ node.io_ctx, nano::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } };
	boost::asio::ip::tcp::socket client{ node.io_ctx };
	client.connect (acceptor.local_endpoint ());
	auto accepted = acceptor.accept ();
	auto const remote_endpoint = accepted.remote_endpoint ();

	// The peer resets the connection, a close with no linger time sends a reset in place of a graceful close
	client.set_option (boost::asio::socket_base::linger{ /* enabled */ true, /* timeout */ 0 });
	client.close ();

	// The reset arrives shortly after, and from then on the connection cannot tell its peer's endpoint
	auto const endpoint_unreadable = [&accepted] () {
		boost::system::error_code ec;
		accepted.remote_endpoint (ec);
		return ec.failed ();
	};
	if (system.poll_until_true (5s, endpoint_unreadable))
	{
		GTEST_SKIP () << "The endpoint of a peer that reset the connection is still readable here";
	}

	// The socket reports the endpoint it was given
	auto const socket = std::make_shared<nano::transport::tcp_socket> (node, std::move (accepted), remote_endpoint);
	ASSERT_EQ (remote_endpoint, socket->get_remote_endpoint ());
}
