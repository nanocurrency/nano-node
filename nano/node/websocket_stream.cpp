#include <nano/node/websocket_stream.hpp>

#include <boost/asio/bind_executor.hpp>

namespace
{
/** Type-erasing wrapper for websocket streams */
template <typename stream_type>
class stream_wrapper : public nano::websocket::websocket_stream_concept
{
public:
	stream_wrapper (socket_type socket_a) :
		ws (std::move (socket_a)), strand (ws.get_executor ())
	{
		ws.text (true);
	}

	void handshake (std::function<void (boost::system::error_code const & ec)> callback_a) override
	{
		// Websocket handshake
		ws.async_accept ([callback_a] (boost::system::error_code const & ec) {
			callback_a (ec);
		});
	}

	boost::asio::strand<boost::asio::io_context::executor_type> & get_strand () override
	{
		return strand;
	}

	socket_type & get_socket () override
	{
		return ws.next_layer ();
	}

	void close (boost::beast::websocket::close_reason const & reason_a, boost::system::error_code & ec_a) override
	{
		ws.close (reason_a, ec_a);
	}

	void async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) override
	{
		ws.async_write (buffer_a, boost::asio::bind_executor (strand, callback_a));
	}

	void async_read (boost::beast::multi_buffer & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a) override
	{
		ws.async_read (buffer_a, boost::asio::bind_executor (strand, callback_a));
	}

private:
	stream_type ws;
	boost::asio::strand<boost::asio::io_context::executor_type> strand;
};
}

nano::websocket::stream::stream (socket_type socket_a)
{
	impl = std::make_unique<stream_wrapper<ws_type>> (std::move (socket_a));
}

[[nodiscard]] boost::asio::strand<boost::asio::io_context::executor_type> & nano::websocket::stream::get_strand ()
{
	return impl->get_strand ();
}

[[nodiscard]] socket_type & nano::websocket::stream::get_socket ()
{
	return impl->get_socket ();
}

void nano::websocket::stream::handshake (std::function<void (boost::system::error_code const & ec)> callback_a)
{
	impl->handshake (callback_a);
}

void nano::websocket::stream::close (boost::beast::websocket::close_reason const & reason_a, boost::system::error_code & ec_a)
{
	impl->close (reason_a, ec_a);
}

void nano::websocket::stream::async_write (nano::shared_const_buffer const & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a)
{
	impl->async_write (buffer_a, callback_a);
}

void nano::websocket::stream::async_read (boost::beast::multi_buffer & buffer_a, std::function<void (boost::system::error_code, std::size_t)> callback_a)
{
	impl->async_read (buffer_a, callback_a);
}
