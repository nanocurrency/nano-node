#pragma once

#include <nano/lib/asio.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/messages/message.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/common.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace nano::test
{
/**
 * A peer's side of the node id handshake: the node id it claims and the two messages that say so.
 * `handshake_v3 ()` and `handshake_v2 ()` make valid ones. A test of a malformed handshake builds a message from one of them, changes it and sends it itself
 */
struct peer_handshake
{
	nano::account node_id; // Node id the response claims, which the node knows the peer's channel by once it accepts the handshake

	// Builds the message that asks the node to prove its id, with a cookie of its own each time
	std::function<nano::messages::node_id_handshake ()> query;

	// Builds the message that answers the node's query, which carried `cookie`
	std::function<nano::messages::node_id_handshake (nano::uint256_union const & cookie)> response;
};

// What a ready-made handshake presents to the node
struct handshake_options
{
	nano::keypair node_id{}; // Identity the response is signed with, a new one by default
	std::optional<uint8_t> protocol_version; // Protocol version stamped on both messages, the network's current one by default
	nano::node_capabilities_flags capabilities{}; // Capabilities announced, in a format that carries them
};

// A valid handshake in the current format, which announces the peer's capabilities
nano::test::peer_handshake handshake_v3 (nano::test::handshake_options const & = {});

// A valid handshake in the format of nodes that predate the capability exchange
nano::test::peer_handshake handshake_v2 (nano::test::handshake_options const & = {});

/**
 * Stands in for a remote node on one TCP connection with a node under test.
 * It has nothing behind it: it sends only what the test tells it to and keeps every message the node sends it,
 * so the node's side of the connection runs production code from the listener to the message processor
 */
class peer final
{
public:
	static constexpr std::chrono::seconds timeout{ 5 }; // How long connecting, a send and each step of the handshake may take

	// Connects to the node's listener without starting the handshake, throws if no connection is made
	explicit peer (nano::node &);
	~peer ();

	// Node id of the handshake this peer performed, zero before it performs one
	nano::account node_id () const;

	// This peer's end of the connection, which the node's channel has as its remote endpoint
	nano::tcp_endpoint endpoint () const;

	// Exchanges the messages of a handshake with the node, true once the node holds a channel for this peer under the handshake's node id
	bool handshake (nano::test::peer_handshake const & = nano::test::handshake_v3 ());

	// Sends a message to the node, false if the connection is gone
	bool send (nano::messages::message const &);

	// Sends bytes as they are, for input that no message serializes to
	bool send_bytes (nano::buffer_view);

	// Messages of one type received from the node so far, in order of arrival
	template <typename Message>
	std::deque<Message> received () const;

	// Ends the connection from the peer's side
	void close ();

	// Whether the connection is open, false once either side has ended it
	bool alive () const;

private:
	friend class peer_listener;

	// Takes over a connection the node opened and a listener accepted
	peer (nano::node &, boost::asio::ip::tcp::socket accepted);

	// Starts the io thread, with no connection yet
	peer (nano::node &, nano::transport::connection_type);

	// Runs a coroutine on the io thread and waits for its result
	template <typename T>
	T run (boost::asio::awaitable<T>);

	boost::asio::awaitable<boost::system::error_code> connect ();
	boost::asio::awaitable<boost::system::error_code> write (nano::buffer_view);

	// Reads messages into the inbox until the connection ends
	boost::asio::awaitable<void> read_messages ();

	// Marks the connected socket open and starts reading from it
	void start_reading ();

	// Waits for the condition, false if it does not come true within the timeout
	bool wait_until (std::function<bool ()> const & condition) const;

	void stop ();

private:
	nano::node & node;
	nano::transport::connection_type const connection; // Which side opened the connection, as the node sees it
	nano::account handshake_node_id{ 0 }; // Node id of the handshake this peer performed

	boost::asio::io_context io_ctx;
	boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard;
	boost::asio::ip::tcp::socket socket;
	nano::tcp_endpoint local_endpoint;

	mutable std::mutex mutex;

	// Every message received from the node, in order of arrival
	std::deque<std::shared_ptr<nano::messages::message const>> inbox;

	bool open{ false };

	// Runs the io context, so the peer keeps reading whatever the test thread does. Declared last, so it starts once every other member exists
	std::thread thread;
};

/**
 * Listens on a loopback port for connections the node under test opens, and hands each of them out as a peer.
 * The kernel completes a connection whether or not a test accepts it, so a listener nobody accepts from still holds the node's connections open
 */
class peer_listener final
{
public:
	explicit peer_listener (nano::node &);

	// Where the listener takes connections, for a test to point the node at
	nano::endpoint endpoint () const;

	// The next connection the node opened, as a peer that has not started the handshake. Null if none arrives within the peer timeout
	std::unique_ptr<nano::test::peer> accept ();

private:
	nano::node & node;

	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
};
}

template <typename Message>
std::deque<Message> nano::test::peer::received () const
{
	std::deque<Message> result;
	std::lock_guard guard{ mutex };
	for (auto const & message : inbox)
	{
		if (auto const * typed = dynamic_cast<Message const *> (message.get ()))
		{
			result.push_back (*typed);
		}
	}
	return result;
}
