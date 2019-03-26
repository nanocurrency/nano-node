#pragma once

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <nano/lib/blocks.hpp>
#include <nano/lib/numbers.hpp>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace nano
{
class node;
namespace websocket
{
	class listener;

	/** Supported topics */
	enum class topic
	{
		invalid = 0,
		/** Acknowledgement of prior incoming message */
		ack,
		/** A confirmation message */
		confirmation
	};

	/** A message queued for broadcasting */
	class message final
	{
	public:
		message (nano::websocket::topic topic_a) :
		topic (topic_a)
		{
		}
		message (nano::websocket::topic topic_a, boost::property_tree::ptree & tree_a) :
		topic (topic_a), contents (tree_a)
		{
		}

		std::string to_string ();
		nano::websocket::topic topic;
		boost::property_tree::ptree contents;
	};

	/** Message builder. This is expanded with new builder functions are necessary. */
	class message_builder final
	{
	public:
		message block_confirmed (std::shared_ptr<nano::block> block_a, nano::account const & account_a, nano::amount const & amount_a, std::string subtype);
	};

	/** A websocket session managing its own lifetime */
	class session final : public std::enable_shared_from_this<session>
	{
	public:
		/** Constructor that takes ownership over \p socket_a */
		explicit session (nano::websocket::listener & listener_a, boost::asio::ip::tcp::socket socket_a);
		~session ();

		/** Perform Websocket handshake and start reading messages */
		void handshake ();

		/** Close the websocket and end the session */
		void close ();

		/** Read the next message. This implicitely handles incoming websocket pings. */
		void read ();

		/** Enqueue \p message_a for writing to the websockets */
		void write (nano::websocket::message message_a);

	private:
		/** The owning listener */
		nano::websocket::listener & ws_listener;
		/** Websocket */
		boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws;
		/** Buffer for received messages */
		boost::beast::multi_buffer read_buffer;
		/** All websocket writes and updates to send_queue must go through the write strand. */
		boost::asio::strand<boost::asio::io_context::executor_type> write_strand;
		/** Outgoing messages. The send queue is protected by accessing it only through the write strand */
		std::deque<message> send_queue;
		/** Serialize calls to websocket::stream initiating functions */
		std::mutex io_mutex;

		/** Hash functor for topic enums */
		struct topic_hash
		{
			template <typename T>
			std::size_t operator() (T t) const
			{
				return static_cast<std::size_t> (t);
			}
		};
		/**
		 * Set of subscriptions registered by this session. In the future, contextual information
		 * can be added to subscription objects, such as which accounts to get confirmations for.
		 */
		std::unordered_set<topic, topic_hash> subscriptions;
		std::mutex subscriptions_mutex;

		/** Handle incoming message */
		void handle_message (boost::property_tree::ptree const & message_a);
		/** Acknowledge incoming message */
		void send_ack (std::string action_a, std::string id_a);
		/** Send all queued messages. This must be called from the write strand. */
		void write_queued_messages ();
	};

	/** Creates a new session for each incoming connection */
	class listener final : public std::enable_shared_from_this<listener>
	{
	public:
		listener (nano::node & node_a, boost::asio::ip::tcp::endpoint endpoint_a);

		/** Start accepting connections */
		void run ();
		void accept ();
		void on_accept (boost::system::error_code ec_a);

		/** Close all websocket sessions and stop listening for new connections */
		void stop ();

		/** Broadcast \p message to all session subscribing to the message topic. */
		void broadcast (nano::websocket::message message_a);

		nano::node & get_node () const
		{
			return node;
		}

	private:
		nano::node & node;
		boost::asio::ip::tcp::acceptor acceptor;
		boost::asio::ip::tcp::socket socket;
		std::mutex sessions_mutex;
		std::vector<std::weak_ptr<session>> sessions;
		std::atomic<bool> stopped{ false };
	};
}
}
