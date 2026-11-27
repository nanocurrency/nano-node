#pragma once

#include <nano/lib/stream.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/common.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/tcp_socket.hpp>

#include <atomic>
#include <optional>
#include <system_error>

namespace nano::transport
{
class tcp_server final : public std::enable_shared_from_this<tcp_server>
{
public:
	tcp_server (nano::node &, std::shared_ptr<nano::transport::tcp_socket>);
	~tcp_server ();

	// Runs the handshake, then the connection; `callback` gets the handshake's outcome
	void start (nano::transport::connect_callback callback = nullptr);

	void close ();
	void close_async (); // Safe to call from io context

	bool alive () const;

public:
	nano::endpoint get_remote_endpoint () const
	{
		return socket->get_remote_endpoint ();
	}
	nano::endpoint get_local_endpoint () const
	{
		return socket->get_local_endpoint ();
	}
	nano::transport::socket_type get_type () const
	{
		return socket->type ();
	}

private:
	enum class handshake_status
	{
		abort,
		handshake,
		realtime,
	};

	// What a handshake message, or the whole handshake, led to
	struct handshake_result
	{
		handshake_status status;
		std::error_code error{}; // Why the handshake ended, set with `abort`
	};

	void stop ();

	asio::awaitable<void> start_impl (nano::transport::connect_callback callback);
	// Serves the connection: the handshake, then realtime messages until one is invalid
	asio::awaitable<nano::io_result<void>> run (nano::transport::connect_callback callback);
	// Exchanges handshake messages until the connection switches to realtime mode or the handshake is aborted, and why it was
	asio::awaitable<nano::io_result<handshake_result>> perform_handshake ();
	// Receives realtime messages until one is invalid
	asio::awaitable<nano::io_result<void>> run_realtime ();

	// Reads the next message, refusing one of any other type than `expected_type` as an invalid type before its payload is read
	asio::awaitable<nano::io_result<nano::deserialize_message_result>> receive_message (std::optional<nano::messages::message_type> expected_type = std::nullopt);
	asio::awaitable<nano::io_result<nano::deserialize_message_result>> receive_message_impl (std::optional<nano::messages::message_type> expected_type);
	// Reads `size` bytes into the buffer and returns a view of them
	asio::awaitable<nano::io_result<nano::buffer_view>> read_socket (size_t size) const;

	// Handles a handshake message from the peer, answering the query it carries
	asio::awaitable<nano::io_result<handshake_result>> process_handshake (nano::messages::node_id_handshake const & message);
	// Sends a handshake response together with this node's own query
	asio::awaitable<nano::io_result<void>> send_handshake_response (nano::messages::node_id_handshake::response_payload const & response);
	// Sends the query that opens a handshake
	asio::awaitable<nano::io_result<void>> send_handshake_request ();

private:
	nano::node & node;

	std::shared_ptr<nano::transport::tcp_socket> socket;
	std::shared_ptr<nano::transport::tcp_channel> channel; // Every realtime connection must have an associated channel

	nano::async::strand & strand; // The socket's, whose operations run only on it
	nano::async::task task;

	nano::shared_buffer buffer;
	// Larger than any payload a header length field can request, so a read can never exceed it.
	static size_t constexpr max_buffer_size = 65 * 1024;

	std::atomic<bool> handshake_received{ false };

	// Cookie of the handshake query sent on this connection, which the peer's response has to sign, empty before the query and once a response used it
	std::optional<nano::uint256_union> handshake_cookie;

private:
	// Registers a channel for the peer and switches the connection to realtime, returns why it could not
	std::error_code to_realtime_connection (nano::transport::peer_info const &);

private: // Visitors
	class realtime_message_visitor : public nano::messages::message_visitor
	{
	public:
		bool process{ false }; // Whether the message should be passed on for processing
		nano::filter_digest digest{ 0 }; // Digest under which the message entered the duplicate filter, zero if it did not

		void keepalive (nano::messages::keepalive const &) override;
		void publish (nano::messages::publish const &) override;
		void confirm_req (nano::messages::confirm_req const &) override;
		void confirm_ack (nano::messages::confirm_ack const &) override;
		void telemetry_req (nano::messages::telemetry_req const &) override;
		void telemetry_ack (nano::messages::telemetry_ack const &) override;
		void asc_pull_req (nano::messages::asc_pull_req const &) override;
		void asc_pull_ack (nano::messages::asc_pull_ack const &) override;
		void vote_relay_req (nano::messages::vote_relay_req const &) override;
		void vote_relay_ack (nano::messages::vote_relay_ack const &) override;
	};
};
}
