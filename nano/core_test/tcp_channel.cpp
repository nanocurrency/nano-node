#include <nano/boost/asio/ip/address_v6.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>

#include <memory>

using namespace std::chrono_literals;

namespace
{
// A tcp channel over a loopback connection that the node has not registered, so the test's sends are the only traffic on it
class unregistered_channel final
{
public:
	explicit unregistered_channel (nano::node &);

	boost::asio::ip::tcp::acceptor acceptor;
	boost::asio::ip::tcp::socket client; // The far end of the connection, which never reads
	std::shared_ptr<nano::transport::tcp_socket> socket; // The node's end
	std::shared_ptr<nano::transport::tcp_channel> channel;
};

unregistered_channel::unregistered_channel (nano::node & node) :
	acceptor{ node.io_ctx, nano::endpoint{ boost::asio::ip::address_v6::loopback (), 0 } },
	client{ node.io_ctx }
{
	client.connect (acceptor.local_endpoint ());
	auto accepted = acceptor.accept ();
	auto const remote_endpoint = accepted.remote_endpoint ();
	socket = std::make_shared<nano::transport::tcp_socket> (node, std::move (accepted), remote_endpoint);

	nano::transport::peer_info const peer{ .node_id = nano::keypair{}.pub, .protocol_version = node.network_params.network.protocol_version };
	channel = std::make_shared<nano::transport::tcp_channel> (node, socket, peer);
}

// Leaves the outbound limiter that the traffic type is charged to with nothing to grant for longer than a test runs
void starve_bandwidth (nano::node & node, nano::transport::traffic_type type)
{
	// A full bucket grants one request larger than its capacity and stays overdrawn by it
	node.outbound_limiter.reset (nano::rate_limit{ /* rate */ 1, /* burst ratio */ 1.0 }, type);
	EXPECT_TRUE (node.outbound_limiter.try_consume (/* size */ 1024 * 1024, type));
}
}

/*
 * A send waiting for outbound bandwidth when its socket closes is failed with operation_aborted, instead of waiting for bandwidth that may not return.
 * The limiter has nothing to grant and the channel is not registered, so the send is the only one and only the channel itself can end its wait
 */
TEST (tcp_channel, socket_close_ends_bandwidth_wait)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	unregistered_channel connection{ node };

	// With no bandwidth to take, the channel's first send waits for the limiter
	starve_bandwidth (node, nano::transport::traffic_type::test);
	nano::messages::keepalive const keepalive{ nano::dev::network_params.network };
	nano::test::shared_result<boost::system::error_code> result;
	nano::test::shared_counter calls;
	ASSERT_TRUE (connection.channel->send (keepalive, nano::transport::traffic_type::test, [result, calls] (boost::system::error_code const & ec) {
		result.set (ec);
		calls.inc ();
	}));
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_bandwidth, nano::stat::dir::out) >= 1);
	ASSERT_FALSE (result.is_set ());

	// The socket goes away while the send still waits
	connection.socket->close ();

	// The send is reported once, as aborted, and was never written
	ASSERT_TIMELY (5s, result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_EQ (boost::asio::error::operation_aborted, ec);
	ASSERT_EQ (1, calls.value ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_channel, nano::stat::detail::send, nano::stat::dir::out));
}

/*
 * A send waiting for outbound bandwidth when its channel is closed is failed with operation_aborted by the time the close returns.
 * Closing cancels the sending coroutine, whose wait has to hand the cancellation back as an error for the send to be reported at all
 */
TEST (tcp_channel, close_ends_bandwidth_wait)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	unregistered_channel connection{ node };

	// With no bandwidth to take, the channel's first send waits for the limiter
	starve_bandwidth (node, nano::transport::traffic_type::test);
	nano::messages::keepalive const keepalive{ nano::dev::network_params.network };
	nano::test::shared_result<boost::system::error_code> result;
	nano::test::shared_counter calls;
	ASSERT_TRUE (connection.channel->send (keepalive, nano::transport::traffic_type::test, [result, calls] (boost::system::error_code const & ec) {
		result.set (ec);
		calls.inc ();
	}));
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_bandwidth, nano::stat::dir::out) >= 1);
	ASSERT_FALSE (result.is_set ());

	// Closing the channel cancels the sending coroutine and waits for it to end
	connection.channel->close ();

	// The send was reported once, as aborted, before the close returned, and was never written
	ASSERT_TRUE (result.is_set ());
	auto const [ec] = result.get ();
	ASSERT_EQ (boost::asio::error::operation_aborted, ec);
	ASSERT_EQ (1, calls.value ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_channel, nano::stat::detail::send, nano::stat::dir::out));
}
