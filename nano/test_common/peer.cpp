#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/deserialize_message.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_channels.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/network_params.hpp>
#include <nano/test_common/peer.hpp>

#include <gtest/gtest.h>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

/*
 * peer_handshake
 */

namespace
{
// Stamps a handshake message with the protocol version its sender claims to run
void stamp_version (nano::messages::node_id_handshake & message, std::optional<uint8_t> const & protocol_version)
{
	if (protocol_version)
	{
		message.header.version_max = *protocol_version;
		message.header.version_using = *protocol_version;
		message.header.version_min = std::min (message.header.version_min, *protocol_version);
	}
}

// A query with a new cookie, announcing the v2 format and, for a sender that speaks it, v3
nano::messages::node_id_handshake make_query (nano::test::handshake_options const & options, bool announce_v3)
{
	nano::messages::node_id_handshake message{ nano::dev::network_params.network, nano::messages::node_id_handshake::query_payload{ nano::random_pool::generate<nano::uint256_union> () } };
	message.header.flag_set (nano::messages::node_id_handshake::v3_flag, announce_v3);
	stamp_version (message, options.protocol_version);
	return message;
}

// A response over the cookie, signed with the identity of the options
nano::messages::node_id_handshake make_response (nano::test::handshake_options const & options, nano::messages::node_id_handshake::response_payload response, nano::uint256_union const & cookie)
{
	response.node_id = options.node_id.pub;
	response.sign (cookie, options.node_id);
	nano::messages::node_id_handshake message{ nano::dev::network_params.network, std::nullopt, response };
	stamp_version (message, options.protocol_version);
	return message;
}
}

nano::test::peer_handshake nano::test::handshake_v3 (nano::test::handshake_options const & options)
{
	nano::test::peer_handshake handshake;
	handshake.node_id = options.node_id.pub;
	handshake.query = [options] () {
		return make_query (options, /* announce v3 */ true);
	};
	handshake.response = [options] (nano::uint256_union const & cookie) {
		nano::messages::node_id_handshake::response_payload response;
		response.ext = nano::messages::node_id_handshake::response_payload::v3_payload{
			.salt = nano::random_pool::generate<nano::uint256_union> (),
			.genesis = nano::dev::genesis->hash (),
			.flags = options.capabilities,
		};
		return make_response (options, response, cookie);
	};
	return handshake;
}

nano::test::peer_handshake nano::test::handshake_v2 (nano::test::handshake_options const & options)
{
	nano::test::peer_handshake handshake;
	handshake.node_id = options.node_id.pub;
	handshake.query = [options] () {
		// Without the v3 announcement the node answers in v2
		return make_query (options, /* announce v3 */ false);
	};
	handshake.response = [options] (nano::uint256_union const & cookie) {
		nano::messages::node_id_handshake::response_payload response;
		response.ext = nano::messages::node_id_handshake::response_payload::v2_payload{
			.salt = nano::random_pool::generate<nano::uint256_union> (),
			.genesis = nano::dev::genesis->hash (),
		};
		return make_response (options, response, cookie);
	};
	return handshake;
}

/*
 * peer
 */

nano::test::peer::peer (nano::node & node_a, nano::transport::connection_type connection_a) :
	node{ node_a },
	connection{ connection_a },
	io_guard{ boost::asio::make_work_guard (io_ctx) },
	socket{ io_ctx },
	thread{ [this] () {
		io_ctx.run ();
	} }
{
}

nano::test::peer::peer (nano::node & node_a) :
	peer{ node_a, nano::transport::connection_type::inbound }
{
	auto const ec = run (connect ());
	if (ec)
	{
		// The delegated constructor has completed, so this throw runs the destructor, which joins the io thread
		throw std::runtime_error{ "Test peer could not connect to the node: " + ec.message () };
	}
}

nano::test::peer::peer (nano::node & node_a, boost::asio::ip::tcp::socket accepted) :
	peer{ node_a, nano::transport::connection_type::outbound }
{
	// The connection moves from the listener's io context to this peer's
	auto const protocol = accepted.local_endpoint ().protocol ();
	auto const handle = accepted.release ();
	boost::asio::post (io_ctx, boost::asio::use_future ([this, protocol, handle] () {
		socket.assign (protocol, handle);
		start_reading ();
	}))
	.get ();
}

nano::test::peer::~peer ()
{
	stop ();
}

void nano::test::peer::stop ()
{
	if (!thread.joinable ())
	{
		return;
	}
	close ();
	io_guard.reset ();
	io_ctx.stop ();
	thread.join ();
}

nano::account nano::test::peer::node_id () const
{
	return handshake_node_id;
}

nano::tcp_endpoint nano::test::peer::endpoint () const
{
	return local_endpoint;
}

bool nano::test::peer::handshake (nano::test::peer_handshake const & handshake)
{
	release_assert (handshake.query && handshake.response);
	handshake_node_id = handshake.node_id;

	// The cookie of the query the node sent, once it has arrived
	auto const node_cookie = [this] () -> std::optional<nano::uint256_union> {
		for (auto const & message : received<nano::messages::node_id_handshake> ())
		{
			if (message.query)
			{
				return message.query->cookie;
			}
		}
		return std::nullopt;
	};
	auto const node_asked = [&node_cookie] () {
		return node_cookie ().has_value ();
	};

	auto query = handshake.query ();
	switch (connection)
	{
		case nano::transport::connection_type::inbound:
		{
			// The peer opens with its query, the node answers it together with a query of its own, and the peer answers that one
			if (!send (query))
			{
				return false;
			}
			if (!wait_until (node_asked))
			{
				return false;
			}
			if (!send (handshake.response (*node_cookie ())))
			{
				return false;
			}
		}
		break;
		case nano::transport::connection_type::outbound:
		{
			// The node opens with its query, and the peer answers it together with a query of its own, both in one message as a node sends them
			if (!wait_until (node_asked))
			{
				return false;
			}
			auto message = handshake.response (*node_cookie ());
			message.query = query.query;
			message.header.flag_set (nano::messages::node_id_handshake::query_flag, query.query.has_value ());
			if (!send (message))
			{
				return false;
			}
		}
		break;
	}

	// The node proves its identity by signing the cookie of this peer's query
	if (!query.query)
	{
		return false;
	}
	auto const node_answered = [this, cookie = query.query->cookie] () {
		auto const handshakes = received<nano::messages::node_id_handshake> ();
		return std::any_of (handshakes.begin (), handshakes.end (), [&cookie] (auto const & message) {
			return message.response && message.response->validate (cookie);
		});
	};
	if (!wait_until (node_answered))
	{
		return false;
	}

	// The node has accepted the handshake once its channel for this connection carries the node id the peer claimed
	auto const node_has_channel = [this] () {
		auto const channel = node.network.tcp_channels.find_channel (local_endpoint);
		return channel != nullptr && channel->get_node_id () == handshake_node_id;
	};
	return wait_until (node_has_channel);
}

bool nano::test::peer::send (nano::messages::message const & message)
{
	auto const bytes = message.to_bytes ();
	return send_bytes (*bytes);
}

bool nano::test::peer::send_bytes (nano::buffer_view bytes)
{
	return !run (write (bytes));
}

void nano::test::peer::close ()
{
	boost::asio::post (io_ctx, boost::asio::use_future ([this] () {
		boost::system::error_code ignored;
		socket.close (ignored);
	}))
	.get ();

	std::lock_guard guard{ mutex };
	open = false;
}

bool nano::test::peer::alive () const
{
	std::lock_guard guard{ mutex };
	return open;
}

template <typename T>
T nano::test::peer::run (boost::asio::awaitable<T> task)
{
	return boost::asio::co_spawn (io_ctx, std::move (task), boost::asio::use_future).get ();
}

auto nano::test::peer::connect () -> boost::asio::awaitable<boost::system::error_code>
{
	// A connect the node never answers is cancelled at the timeout
	boost::asio::steady_timer deadline{ io_ctx, timeout };
	deadline.async_wait ([this] (boost::system::error_code const & ec) {
		if (!ec)
		{
			socket.close ();
		}
	});

	auto const [ec] = co_await socket.async_connect (nano::transport::map_endpoint_to_tcp (node.network.endpoint ()), boost::asio::as_tuple (boost::asio::use_awaitable));
	deadline.cancel ();
	if (ec)
	{
		co_return ec;
	}

	start_reading ();

	co_return boost::system::error_code{};
}

void nano::test::peer::start_reading ()
{
	local_endpoint = socket.local_endpoint ();
	{
		std::lock_guard guard{ mutex };
		open = true;
	}
	boost::asio::co_spawn (io_ctx, read_messages (), boost::asio::detached);
}

auto nano::test::peer::write (nano::buffer_view bytes) -> boost::asio::awaitable<boost::system::error_code>
{
	// A write the node never reads is cancelled at the timeout
	boost::asio::steady_timer deadline{ io_ctx, timeout };
	deadline.async_wait ([this] (boost::system::error_code const & ec) {
		if (!ec)
		{
			socket.close ();
		}
	});

	auto const [ec, size] = co_await boost::asio::async_write (socket, boost::asio::buffer (bytes.data (), bytes.size ()), boost::asio::as_tuple (boost::asio::use_awaitable));
	deadline.cancel ();

	co_return ec;
}

auto nano::test::peer::read_messages () -> boost::asio::awaitable<void>
{
	// Larger than any payload a header can announce
	std::vector<uint8_t> buffer (nano::messages::message_header::size + 2 * std::numeric_limits<uint16_t>::max ());

	while (true)
	{
		auto const [header_ec, header_size] = co_await boost::asio::async_read (socket, boost::asio::buffer (buffer.data (), nano::messages::message_header::size), boost::asio::as_tuple (boost::asio::use_awaitable));
		if (header_ec)
		{
			break; // The connection ended
		}

		auto const [header, header_status] = nano::deserialize_message_header (nano::buffer_view{ buffer.data (), nano::messages::message_header::size }, node.network_params.network);
		if (!header)
		{
			ADD_FAILURE () << "The node sent a header the test peer cannot parse: " << to_string (header_status);
			break;
		}

		auto const payload_size = header->payload_length_bytes ();
		auto const [payload_ec, payload_read] = co_await boost::asio::async_read (socket, boost::asio::buffer (buffer.data (), payload_size), boost::asio::as_tuple (boost::asio::use_awaitable));
		if (payload_ec)
		{
			break; // The connection ended
		}

		auto [message, status] = nano::deserialize_message_payload (nano::buffer_view{ buffer.data (), payload_size }, *header, node.network_params.network);
		if (!message)
		{
			ADD_FAILURE () << "The node sent a message the test peer cannot parse: " << to_string (status);
			break;
		}

		std::lock_guard guard{ mutex };
		inbox.push_back (std::move (message));
	}

	boost::system::error_code ignored;
	socket.close (ignored);

	std::lock_guard guard{ mutex };
	open = false;
}

bool nano::test::peer::wait_until (std::function<bool ()> const & condition) const
{
	auto const deadline = std::chrono::steady_clock::now () + timeout;
	while (!condition ())
	{
		// Nothing more arrives on a connection that has ended
		if (!alive () || std::chrono::steady_clock::now () > deadline)
		{
			return condition ();
		}
		std::this_thread::sleep_for (1ms);
	}
	return true;
}

/*
 * peer_listener
 */

nano::test::peer_listener::peer_listener (nano::node & node_a) :
	node{ node_a },
	// Without address reuse, so the port the kernel picks is this acceptor's alone
	acceptor{ io_ctx, nano::tcp_endpoint{ boost::asio::ip::address_v6::loopback (), 0 }, /* reuse address */ false }
{
}

nano::endpoint nano::test::peer_listener::endpoint () const
{
	return nano::transport::map_tcp_to_endpoint (acceptor.local_endpoint ());
}

std::unique_ptr<nano::test::peer> nano::test::peer_listener::accept ()
{
	boost::asio::ip::tcp::socket accepted{ io_ctx };
	std::optional<boost::system::error_code> result;
	acceptor.async_accept (accepted, [&result] (boost::system::error_code const & ec) {
		result = ec;
	});

	// The accept runs on the calling thread. It is the only work this io context has, so run_for returns as soon as it completes, or at the timeout
	io_ctx.restart ();
	io_ctx.run_for (nano::test::peer::timeout);
	if (!result)
	{
		// Nothing connected in time, so the pending accept is cancelled before its socket goes away
		acceptor.cancel ();
		io_ctx.restart ();
		io_ctx.run ();
		return nullptr;
	}
	if (*result)
	{
		return nullptr;
	}

	return std::unique_ptr<nano::test::peer>{ new nano::test::peer{ node, std::move (accepted) } };
}
