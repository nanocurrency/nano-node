#include <celerix/lib/blocks.hpp>
#include <celerix/lib/stream.hpp>
#include <celerix/node/transport/block_deserializer.hpp>
#include <celerix/node/transport/tcp_socket.hpp>

celerix::transport::block_deserializer::block_deserializer () :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () }
{
}

void celerix::transport::block_deserializer::read (celerix::transport::tcp_socket & socket, callback_type const && callback)
{
	debug_assert (callback);
	read_buffer->resize (1);
	socket.async_read (read_buffer, 1, [this_l = shared_from_this (), &socket, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec)
		{
			callback (ec, nullptr);
			return;
		}
		if (size_a != 1)
		{
			callback (boost::asio::error::fault, nullptr);
			return;
		}
		this_l->received_type (socket, std::move (callback));
	});
}

void celerix::transport::block_deserializer::received_type (celerix::transport::tcp_socket & socket, callback_type const && callback)
{
	celerix::block_type type = static_cast<celerix::block_type> (read_buffer->data ()[0]);
	if (type == celerix::block_type::not_a_block)
	{
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	auto size = celerix::block::size (type);
	if (size == 0)
	{
		callback (boost::asio::error::fault, nullptr);
		return;
	}
	read_buffer->resize (size);
	socket.async_read (read_buffer, size, [this_l = shared_from_this (), size, type, callback = std::move (callback)] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec)
		{
			callback (ec, nullptr);
			return;
		}
		if (size_a != size)
		{
			callback (boost::asio::error::fault, nullptr);
			return;
		}
		this_l->received_block (type, std::move (callback));
	});
}

void celerix::transport::block_deserializer::received_block (celerix::block_type type, callback_type const && callback)
{
	celerix::bufferstream stream{ read_buffer->data (), read_buffer->size () };
	auto block = celerix::deserialize_block (stream, type);
	callback (boost::system::error_code{}, block);
}
