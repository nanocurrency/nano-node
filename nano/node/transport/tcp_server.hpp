#pragma once

#include <nano/lib/stream.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/fwd.hpp>
#include <nano/node/transport/tcp_socket.hpp>

#include <atomic>

namespace nano::transport
{
class tcp_server final : public std::enable_shared_from_this<tcp_server>
{
public:
	tcp_server (nano::node &, std::shared_ptr<nano::transport::tcp_socket>);
	~tcp_server ();

	void start ();

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

	void stop ();

	asio::awaitable<void> start_impl ();
	asio::awaitable<handshake_status> perform_handshake ();
	asio::awaitable<void> run_realtime ();
	asio::awaitable<nano::deserialize_message_result> receive_message ();
	asio::awaitable<nano::deserialize_message_result> receive_message_impl ();
	asio::awaitable<nano::buffer_view> read_socket (size_t size) const;

	asio::awaitable<handshake_status> process_handshake (nano::messages::node_id_handshake const & message);
	asio::awaitable<void> send_handshake_response (nano::messages::node_id_handshake::query_payload const & query, bool v2);
	asio::awaitable<void> send_handshake_request ();

private:
	nano::node & node;

	std::shared_ptr<nano::transport::tcp_socket> socket;
	std::shared_ptr<nano::transport::tcp_channel> channel; // Every realtime connection must have an associated channel

	nano::async::strand strand;
	nano::async::task task;

	nano::shared_buffer buffer;
	static size_t constexpr max_buffer_size = 64 * 1024; // 64 KB

	std::atomic<bool> handshake_received{ false };

private:
	bool to_bootstrap_connection ();
	bool to_realtime_connection (nano::account const & node_id);

private: // Visitors
	class realtime_message_visitor : public nano::messages::message_visitor
	{
	public:
		bool process{ false };

		void keepalive (nano::messages::keepalive const &) override;
		void publish (nano::messages::publish const &) override;
		void confirm_req (nano::messages::confirm_req const &) override;
		void confirm_ack (nano::messages::confirm_ack const &) override;
		void frontier_req (nano::messages::frontier_req const &) override;
		void telemetry_req (nano::messages::telemetry_req const &) override;
		void telemetry_ack (nano::messages::telemetry_ack const &) override;
		void asc_pull_req (nano::messages::asc_pull_req const &) override;
		void asc_pull_ack (nano::messages::asc_pull_ack const &) override;
	};
};
}
