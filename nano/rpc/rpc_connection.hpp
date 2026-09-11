#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/strand.hpp>
#include <nano/boost/beast/core/flat_buffer.hpp>
#include <nano/boost/beast/http.hpp>
#include <nano/lib/fwd.hpp>

#include <boost/algorithm/string/predicate.hpp>

#include <atomic>

using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;

namespace nano
{
class rpc_config;
class rpc_connection_tracker;
class rpc_handler_interface;

/**
 * One accepted HTTP connection: reads a single request, hands it to the backend and writes the response.
 * A connection is serving a request from the moment the request has been read until its response
 * has been written, and reports both transitions to the tracker of the server that accepted it.
 */
class rpc_connection : public std::enable_shared_from_this<nano::rpc_connection>
{
public:
	rpc_connection (nano::rpc_config const & rpc_config, boost::asio::io_context & io_ctx, nano::logger &, nano::rpc_handler_interface & rpc_handler_interface_a, std::shared_ptr<nano::rpc_connection_tracker>);
	virtual ~rpc_connection () = default;
	virtual void parse_connection ();
	virtual void write_completion_handler (std::shared_ptr<nano::rpc_connection> const & rpc_connection);
	void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);

	// Closes the socket from its strand, safe to call from any thread
	void close ();
	bool serving () const;

	socket_type socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> res;
	boost::asio::strand<boost::asio::io_context::executor_type> strand;
	std::atomic_flag responded;
	boost::asio::io_context & io_ctx;
	nano::logger & logger;
	nano::rpc_config const & rpc_config;
	nano::rpc_handler_interface & rpc_handler_interface;

protected:
	void request_begin ();
	void request_end ();

	std::shared_ptr<nano::rpc_connection_tracker> tracker;
	std::atomic<bool> serving_m{ false };

	template <typename STREAM_TYPE>
	void read (STREAM_TYPE & stream);

	template <typename STREAM_TYPE>
	void parse_request (STREAM_TYPE & stream, std::shared_ptr<boost::beast::http::request_parser<boost::beast::http::empty_body>> const & header_parser);
};
}
