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

	// Runs the handshake and, once it made a channel, the connection. `callback` learns once how the handshake ended: nothing for a channel, the reason otherwise
	void start (nano::transport::connect_callback callback = noop<nano::transport::connect_callback> ());

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
		bootstrap,
	};

	// What a handshake message led to
	struct handshake_result
	{
		handshake_status status;
		std::error_code error{}; // Why the handshake ended, set with `abort`
	};

	void stop ();

	asio::awaitable<void> start_impl (nano::transport::connect_callback callback);

	// Runs the handshake to its end: nothing when it made a channel, the reason otherwise. Socket errors are thrown
	asio::awaitable<std::error_code> perform_handshake ();
	// Whether the connection has been open for as long as a handshake may take, or its socket gave up waiting
	bool handshake_timed_out () const;

	asio::awaitable<void> run_realtime ();

	// Reads the next message, refusing one of any other type than `expected_type` as an invalid type before its payload is read
	asio::awaitable<nano::deserialize_message_result> receive_message (std::optional<nano::messages::message_type> expected_type = std::nullopt);
	asio::awaitable<nano::deserialize_message_result> receive_message_impl (std::optional<nano::messages::message_type> expected_type);
	asio::awaitable<nano::buffer_view> read_socket (size_t size) const;

	asio::awaitable<handshake_result> process_handshake (nano::messages::node_id_handshake const & message);
	asio::awaitable<void> send_handshake_response (nano::messages::node_id_handshake::query_payload const & query, nano::messages::handshake_version version);
	asio::awaitable<void> send_handshake_request ();

private:
	nano::node & node;

	std::shared_ptr<nano::transport::tcp_socket> socket;
	std::shared_ptr<nano::transport::tcp_channel> channel; // Every realtime connection must have an associated channel

	nano::async::strand strand;
	nano::async::task task;

	nano::shared_buffer buffer;
	// Larger than any payload a header length field can request, so a read can never exceed it.
	static size_t constexpr max_buffer_size = 65 * 1024;

	std::atomic<bool> handshake_received{ false };

	// Cookie of the handshake query sent on this connection, which the peer's response has to sign, empty before the query and once a response used it
	std::optional<nano::uint256_union> handshake_cookie;

private:
	bool to_bootstrap_connection ();
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
