#pragma once

#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/socket.hpp>

#include <atomic>
#include <queue>

namespace nano
{
class message;
}

namespace nano::transport
{
class message_deserializer;
class tcp_server;

/**
 * Server side portion of bootstrap sessions. Listens for new socket connections and spawns tcp_server objects when connected.
 */
class tcp_listener final
{
public:
	tcp_listener (uint16_t, nano::node &);
	void start ();
	void stop ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<nano::socket> const &);
	std::size_t connection_count ();

	nano::mutex mutex;
	std::unordered_map<nano::transport::tcp_server *, std::weak_ptr<nano::transport::tcp_server>> connections;
	nano::tcp_endpoint endpoint ();
	nano::node & node;
	std::shared_ptr<nano::server_socket> listening_socket;
	bool on{ false };
	std::atomic<std::size_t> bootstrap_count{ 0 };
	std::atomic<std::size_t> realtime_count{ 0 };
	uint16_t port;
};

std::unique_ptr<container_info_component> collect_container_info (tcp_listener & bootstrap_listener, std::string const & name);

class tcp_server final : public std::enable_shared_from_this<tcp_server>
{
public:
	tcp_server (std::shared_ptr<nano::socket>, std::shared_ptr<nano::node>, bool allow_bootstrap = true);
	~tcp_server ();

	void start ();
	void stop ();

	void timeout ();

	void send_handshake_response (nano::uint256_union query);

	std::shared_ptr<nano::socket> const socket;
	std::shared_ptr<nano::node> const node;
	nano::mutex mutex;
	std::atomic<bool> stopped{ false };
	std::atomic<bool> handshake_query_received{ false };
	// Remote enpoint used to remove response channel even after socket closing
	nano::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	nano::account remote_node_id{};
	std::chrono::steady_clock::time_point last_telemetry_req{ std::chrono::steady_clock::time_point () };

private:
	void receive_message ();
	void received_message (std::unique_ptr<nano::message> message);
	bool process_message (std::unique_ptr<nano::message> message);

	void queue_realtime (std::unique_ptr<nano::message> message);

	bool to_bootstrap_connection ();
	bool to_realtime_connection (nano::account const & node_id);
	bool is_undefined_connection () const;
	bool is_bootstrap_connection () const;
	bool is_realtime_connection () const;

	std::shared_ptr<nano::transport::message_deserializer> message_deserializer;

	bool allow_bootstrap;

private:
	class handshake_message_visitor : public nano::message_visitor
	{
	public:
		bool process{ false };
		bool bootstrap{ false };

		explicit handshake_message_visitor (std::shared_ptr<tcp_server>);

		void node_id_handshake (nano::node_id_handshake const &) override;
		void bulk_pull (nano::bulk_pull const &) override;
		void bulk_pull_account (nano::bulk_pull_account const &) override;
		void bulk_push (nano::bulk_push const &) override;
		void frontier_req (nano::frontier_req const &) override;

	private:
		std::shared_ptr<tcp_server> server;
	};

	class realtime_message_visitor : public nano::message_visitor
	{
	public:
		bool process{ false };

		explicit realtime_message_visitor (tcp_server &);

		void keepalive (nano::keepalive const &) override;
		void publish (nano::publish const &) override;
		void confirm_req (nano::confirm_req const &) override;
		void confirm_ack (nano::confirm_ack const &) override;
		void frontier_req (nano::frontier_req const &) override;
		void telemetry_req (nano::telemetry_req const &) override;
		void telemetry_ack (nano::telemetry_ack const &) override;

	private:
		tcp_server & server;
	};

	class bootstrap_message_visitor : public nano::message_visitor
	{
	public:
		bool processed{ false };

		explicit bootstrap_message_visitor (std::shared_ptr<tcp_server>);

		void bulk_pull (nano::bulk_pull const &) override;
		void bulk_pull_account (nano::bulk_pull_account const &) override;
		void bulk_push (nano::bulk_push const &) override;
		void frontier_req (nano::frontier_req const &) override;

	private:
		std::shared_ptr<tcp_server> server;
	};
};
}
