#pragma once

#include <nano/boost/asio/connect.hpp>
#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/beast/core.hpp>
#include <nano/boost/beast/websocket.hpp>
#include <nano/node/websocket.hpp>

#include <chrono>

using namespace std::chrono_literals;

namespace
{
// Creates its own io context
class fake_websocket_client
{
public:
	fake_websocket_client (unsigned port) :
		socket (std::make_shared<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>> (ioc))
	{
		std::string const host = "::1";
		boost::asio::ip::tcp::resolver resolver{ ioc };
		auto const results = resolver.resolve (host, std::to_string (port));
		boost::asio::connect (socket->next_layer (), results.begin (), results.end ());
		socket->handshake (host, "/");
		socket->text (true);
	}

	~fake_websocket_client ()
	{
		if (socket->is_open ())
		{
			socket->async_close (boost::beast::websocket::close_code::normal, [socket = this->socket] (boost::beast::error_code const & ec) {
				// A synchronous close usually hangs in tests when the server's io_context stops looping
				// An async_close solves this problem
			});
		}
	}

	void send_message (std::string const & message_a)
	{
		socket->write (boost::asio::buffer (message_a));
	}

	void await_ack ()
	{
		debug_assert (socket->is_open ());
		boost::beast::flat_buffer buffer;
		socket->read (buffer);
	}

	boost::optional<std::string> get_response (std::chrono::seconds const deadline = 5s)
	{
		debug_assert (deadline > 0s);
		boost::optional<std::string> result;
		auto buffer (std::make_shared<boost::beast::flat_buffer> ());
		socket->async_read (*buffer, [&result, &buffer, socket = this->socket] (boost::beast::error_code const & ec, std::size_t const /*n*/) {
			if (!ec)
			{
				std::ostringstream res;
				res << beast_buffers (buffer->data ());
				result = res.str ();
			}
		});
		ioc.run_one_for (deadline);
		return result;
	}

private:
	boost::asio::io_context ioc;
	std::shared_ptr<boost::beast::websocket::stream<boost::asio::ip::tcp::socket>> socket;
};
}
