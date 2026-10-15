#pragma once

#include <nano/boost/asio/strand.hpp>
#include <nano/boost/beast/core.hpp>
#include <nano/boost/beast/websocket.hpp>
#include <nano/lib/asio.hpp>

#include <memory>

using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#define beast_buffers boost::beast::make_printable
using ws_type = boost::beast::websocket::stream<socket_type>;

namespace nano::websocket
{
/** The minimal stream interface needed by the Nano websocket implementation */
class websocket_stream_concept
{
public:
	virtual ~websocket_stream_concept () = default;
	virtual boost::asio::strand<boost::asio::io_context::executor_type> & get_strand () = 0;
	virtual socket_type & get_socket () = 0;
	virtual void handshake (std::function<void (boost::system::error_code const & ec)> callback_a) = 0;
	virtual void close (boost::beast::websocket::close_reason const & reason_a, boost::system::error_code & ec_a) = 0;
	virtual void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) = 0;
	virtual void async_read (boost::beast::multi_buffer & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) = 0;
};

/** Websocket stream reached through the minimal stream interface */
class stream final : public websocket_stream_concept
{
public:
	stream (socket_type socket_a);

	[[nodiscard]] boost::asio::strand<boost::asio::io_context::executor_type> & get_strand () override;
	[[nodiscard]] socket_type & get_socket () override;
	void handshake (std::function<void (boost::system::error_code const & ec)> callback_a) override;
	void close (boost::beast::websocket::close_reason const & reason_a, boost::system::error_code & ec_a) override;
	void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) override;
	void async_read (boost::beast::multi_buffer & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) override;

private:
	std::unique_ptr<websocket_stream_concept> impl;
};
}
