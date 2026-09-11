#pragma once

#include <nano/lib/ipc_client.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/rpc_handler_interface.hpp>
#include <nano/lib/rpcconfig.hpp>

#include <atomic>
#include <deque>

namespace nano
{
struct ipc_connection
{
	ipc_connection (nano::ipc::ipc_client && client_a, bool is_available_a) :
		client (std::move (client_a)), is_available (is_available_a)
	{
	}

	nano::ipc::ipc_client client;
	std::atomic<bool> is_available{ false };
};

struct rpc_request
{
	rpc_request (std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		action (action_a), body (body_a), response (response_a)
	{
	}

	rpc_request (int rpc_api_version_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		rpc_api_version (rpc_api_version_a), body (body_a), response (response_a)
	{
	}

	rpc_request (int rpc_api_version_a, std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) :
		rpc_api_version (rpc_api_version_a), action (action_a), body (body_a), response (response_a)
	{
	}

	int rpc_api_version{ 1 };
	std::string action;
	std::string body;
	std::function<void (std::string const &)> response;
};

/**
 * Forwards RPC requests to a node over IPC.
 * When the node acknowledges a `stop` request, `stop_callback` is invoked so the owner can shut
 * this process down; the processor never stops itself or the RPC server.
 */
class rpc_request_processor
{
public:
	rpc_request_processor (std::shared_ptr<boost::asio::io_context> io_ctx, nano::rpc_config & rpc_config, std::function<void ()> stop_callback);
	rpc_request_processor (std::shared_ptr<boost::asio::io_context> io_ctx, nano::rpc_config & rpc_config, std::uint16_t ipc_port_a, std::function<void ()> stop_callback);
	~rpc_request_processor ();
	void stop ();
	void add (std::shared_ptr<rpc_request> const & request);

private:
	void run ();
	void read_payload (std::shared_ptr<nano::ipc_connection> const & connection, std::shared_ptr<std::vector<uint8_t>> const & res, std::shared_ptr<nano::rpc_request> const & rpc_request);
	void try_reconnect_and_execute_request (std::shared_ptr<nano::ipc_connection> const & connection, nano::shared_const_buffer const & req, std::shared_ptr<std::vector<uint8_t>> const & res, std::shared_ptr<nano::rpc_request> const & rpc_request);
	void make_available (nano::ipc_connection & connection);

	std::vector<std::shared_ptr<nano::ipc_connection>> connections;
	nano::mutex request_mutex;
	nano::mutex connections_mutex;
	bool stopped{ false };
	std::deque<std::shared_ptr<nano::rpc_request>> requests;
	nano::condition_variable condition;
	std::string const ipc_address;
	uint16_t const ipc_port;
	std::function<void ()> const stop_callback;
	std::thread thread;
};

class ipc_rpc_processor final : public nano::rpc_handler_interface
{
public:
	ipc_rpc_processor (std::shared_ptr<boost::asio::io_context> io_ctx, nano::rpc_config & rpc_config, std::function<void ()> stop_callback) :
		rpc_request_processor (std::move (io_ctx), rpc_config, std::move (stop_callback))
	{
	}
	ipc_rpc_processor (std::shared_ptr<boost::asio::io_context> io_ctx, nano::rpc_config & rpc_config, std::uint16_t ipc_port_a, std::function<void ()> stop_callback) :
		rpc_request_processor (std::move (io_ctx), rpc_config, ipc_port_a, std::move (stop_callback))
	{
	}

	void process_request (std::string const & action_a, std::string const & body_a, std::function<void (std::string const &)> response_a) override
	{
		rpc_request_processor.add (std::make_shared<nano::rpc_request> (action_a, body_a, response_a));
	}

	void process_request_v2 (rpc_handler_request_params const & params_a, std::string const & body_a, std::function<void (std::shared_ptr<std::string> const &)> response_a) override
	{
		std::string body_l = params_a.json_envelope (body_a);
		rpc_request_processor.add (std::make_shared<nano::rpc_request> (2 /* rpc version */, body_l, [response_a] (std::string const & resp) {
			auto resp_l (std::make_shared<std::string> (resp));
			response_a (resp_l);
		}));
	}

private:
	nano::rpc_request_processor rpc_request_processor;
};
}
