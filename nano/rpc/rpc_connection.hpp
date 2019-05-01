#pragma once

#include <atomic>
#include <boost/beast.hpp>
#include <nano/rpc/rpc_handler.hpp>

namespace nano
{
class logger_mt;
class rpc_config;
class rpc_handler_interface;
class network_constants;

class rpc_connection : public std::enable_shared_from_this<nano::rpc_connection>
{
public:
	rpc_connection (nano::rpc_config const & rpc_config, nano::network_constants const & network_constants, boost::asio::io_context & io_ctx, nano::logger_mt & logger, nano::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc_connection () = default;
	virtual void parse_connection ();
	virtual void write_completion_handler (std::shared_ptr<nano::rpc_connection> rpc_connection);
	void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void parse_request (std::shared_ptr<boost::beast::http::request_parser<boost::beast::http::empty_body>> header_parser);

	void read ();

	boost::asio::ip::tcp::socket socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> res;
	std::atomic_flag responded;
	boost::asio::io_context & io_ctx;
	nano::logger_mt & logger;
	nano::rpc_config const & rpc_config;
	nano::network_constants const & network_constants;
	nano::rpc_handler_interface & rpc_handler_interface;
};
}
