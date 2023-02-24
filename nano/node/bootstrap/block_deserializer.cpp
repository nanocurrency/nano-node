#include <nano/lib/blocks.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/node/transport/socket.hpp>
#include <nano/secure/buffer.hpp>

nano::bootstrap::block_deserializer::block_deserializer () :
	read_buffer{ std::make_shared<std::vector<uint8_t>> () }
{
}

void nano::bootstrap::block_deserializer::read (nano::socket & socket, callback_type const && callback)
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

void nano::bootstrap::block_deserializer::received_type (nano::socket & socket, callback_type const && callback)
{
	nano::block_type type = static_cast<nano::block_type> (read_buffer->data ()[0]);
	if (type == nano::block_type::not_a_block)
	{
		callback (boost::system::error_code{}, nullptr);
		return;
	}
	auto size = nano::block::size (type);
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

void nano::bootstrap::block_deserializer::received_block (nano::block_type type, callback_type const && callback)
{
	nano::bufferstream stream{ read_buffer->data (), read_buffer->size () };
	auto block = nano::deserialize_block (stream, type);
	callback (boost::system::error_code{}, block);
}
