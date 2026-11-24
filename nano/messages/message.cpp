#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/message.hpp>

namespace nano::messages
{
message::message (nano::network_constants const & constants, message_type type_a) :
	header (constants, type_a)
{
}

message::message (message_header const & header_a) :
	header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> message::to_bytes () const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	// The header gives the exact message size, so the vector is allocated once even when the stream flushes a large message into it in several 4 KB chunks
	bytes->reserve (message_header::size + header.payload_length_bytes ());
	nano::vectorstream stream (*bytes);
	serialize (stream);
	return bytes;
}

nano::shared_const_buffer message::to_shared_const_buffer () const
{
	return shared_const_buffer (to_bytes ());
}

message_type message::type () const
{
	return header.type;
}

void message::operator() (nano::object_stream & obs) const
{
	obs.write ("header", header);
}
}
