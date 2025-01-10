#pragma once

#include <celerix/node/endpoint.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/transport/fwd.hpp>
#include <celerix/node/transport/tcp_socket.hpp>

#include <atomic>

namespace celerix
{
class message;
}

namespace celerix::transport
{
class message_deserializer;
class tcp_server;

class tcp_server final : public std::enable_shared_from_this<tcp_server>
{
public:
	tcp_server (std::shared_ptr<celerix::transport::tcp_socket>, std::shared_ptr<celerix::node>, bool allow_bootstrap = true);
	~tcp_server ();

	void start ();
	void stop ();

	void initiate_handshake ();
	void timeout ();
	void set_last_keepalive (celerix::keepalive const & message);
	std::optional<celerix::keepalive> pop_last_keepalive ();

	std::shared_ptr<celerix::transport::tcp_socket> const socket;
	std::weak_ptr<celerix::node> const node;
	celerix::mutex mutex;
	std::atomic<bool> stopped{ false };
	std::atomic<bool> handshake_received{ false };
	// Remote endpoint used to remove response channel even after socket closing
	celerix::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	std::chrono::steady_clock::time_point last_telemetry_req{};

private:
	enum class process_result
	{
		abort,
		progress,
		pause,
	};

	void receive_message ();
	void received_message (std::unique_ptr<celerix::message> message);
	process_result process_message (std::unique_ptr<celerix::message> message);
	void queue_realtime (std::unique_ptr<celerix::message> message);

	bool to_bootstrap_connection ();
	bool to_realtime_connection (celerix::account const & node_id);
	bool is_undefined_connection () const;
	bool is_bootstrap_connection () const;
	bool is_realtime_connection () const;

	enum class handshake_status
	{
		abort,
		handshake,
		realtime,
		bootstrap,
	};

	handshake_status process_handshake (celerix::node_id_handshake const & message);
	void send_handshake_response (celerix::node_id_handshake::query_payload const & query, bool v2);

private:
	bool const allow_bootstrap;
	std::shared_ptr<celerix::transport::message_deserializer> message_deserializer;
	std::optional<celerix::keepalive> last_keepalive;

	// Every realtime connection must have an associated channel
	std::shared_ptr<celerix::transport::tcp_channel> channel;

private: // Visitors
	class handshake_message_visitor : public celerix::message_visitor
	{
	public:
		handshake_status result{ handshake_status::abort };

		explicit handshake_message_visitor (tcp_server & server) :
			server{ server } {};

		void node_id_handshake (celerix::node_id_handshake const &) override;
		void bulk_pull (celerix::bulk_pull const &) override;
		void bulk_pull_account (celerix::bulk_pull_account const &) override;
		void bulk_push (celerix::bulk_push const &) override;
		void frontier_req (celerix::frontier_req const &) override;

	private:
		tcp_server & server;
	};

	class realtime_message_visitor : public celerix::message_visitor
	{
	public:
		bool process{ false };

		explicit realtime_message_visitor (tcp_server & server) :
			server{ server } {};

		void keepalive (celerix::keepalive const &) override;
		void publish (celerix::publish const &) override;
		void confirm_req (celerix::confirm_req const &) override;
		void confirm_ack (celerix::confirm_ack const &) override;
		void frontier_req (celerix::frontier_req const &) override;
		void telemetry_req (celerix::telemetry_req const &) override;
		void telemetry_ack (celerix::telemetry_ack const &) override;
		void asc_pull_req (celerix::asc_pull_req const &) override;
		void asc_pull_ack (celerix::asc_pull_ack const &) override;

	private:
		tcp_server & server;
	};

	class bootstrap_message_visitor : public celerix::message_visitor
	{
	public:
		bool processed{ false };

		explicit bootstrap_message_visitor (std::shared_ptr<tcp_server>);

		void bulk_pull (celerix::bulk_pull const &) override;
		void bulk_pull_account (celerix::bulk_pull_account const &) override;
		void bulk_push (celerix::bulk_push const &) override;
		void frontier_req (celerix::frontier_req const &) override;

	private:
		std::shared_ptr<tcp_server> server;
	};

	friend class handshake_message_visitor;
};
}
