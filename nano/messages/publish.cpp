#include <nano/lib/blocks.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/publish.hpp>

namespace nano::messages
{
publish::publish (bool & error_a, nano::stream & stream_a, message_header const & header_a, nano::network_filter::digest_t const & digest_a, nano::block_uniquer * uniquer_a) :
	message (header_a),
	digest{ digest_a }
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

publish::publish (nano::network_constants const & constants, std::shared_ptr<nano::block> const & block_a, bool is_originator_a) :
	message (constants, message_type::publish),
	block{ block_a }
{
	header.block_type_set (block->type ());
	header.flag_set (originator_flag, is_originator_a);
}

void publish::serialize (nano::stream & stream_a) const
{
	debug_assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool publish::deserialize (nano::stream & stream_a, nano::block_uniquer * uniquer_a)
{
	debug_assert (header.type == message_type::publish);
	block = nano::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void publish::visit (message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool publish::operator== (publish const & other_a) const
{
	return *block == *other_a.block;
}

bool publish::is_originator () const
{
	return header.flag_test (originator_flag);
}

void publish::operator() (nano::object_stream & obs) const
{
	message::operator() (obs); // Write common data

	obs.write ("block", block);
	obs.write ("originator", is_originator ());
}
}
