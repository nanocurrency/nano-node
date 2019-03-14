#pragma once

#include <atomic>
#include <boost/beast.hpp>

namespace nano
{
class logger_mt;

namespace ipc
{
	class ipc_client;
}

class rpc_connection final : public std::enable_shared_from_this<nano::rpc_connection>
{
public:
	rpc_connection (nano::ipc::ipc_client & ipc_client, std::function<void()> stop_callback, boost::asio::io_context & io_ctx, nano::logger_mt & logger);
	void parse_connection (unsigned max_json_depth, bool enable_control);
	void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void read (unsigned max_json_depth, bool enable_control);

	boost::asio::ip::tcp::socket socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::request<boost::beast::http::string_body> request;
	boost::beast::http::response<boost::beast::http::string_body> res;
	std::atomic_flag responded;
	boost::asio::io_context & io_ctx;
	nano::logger_mt & logger;
	nano::ipc::ipc_client & ipc_client;
	std::function<void()> stop_callback;
};
}
