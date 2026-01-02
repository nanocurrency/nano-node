#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/messages/frontier_req.hpp>
#include <nano/messages/message_visitor.hpp>

namespace nano::messages
{
frontier_req::frontier_req (nano::network_constants const & constants) :
	message (constants, message_type::frontier_req)
{
}

frontier_req::frontier_req (bool & error_a, nano::stream & stream_a, message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void frontier_req::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool frontier_req::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == message_type::frontier_req);
	auto error (false);
	try
	{
		nano::read (stream_a, start.bytes);
		nano::read (stream_a, age);
		nano::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void frontier_req::visit (message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool frontier_req::operator== (frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

void frontier_req::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("age", age);
	obs.write ("count", count);
}
}
