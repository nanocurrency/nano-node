#include <gtest/gtest.h>
#include <rai/node/testing.hpp>

namespace {
class test_visitor : public rai::message_visitor
{
public:
	test_visitor () :
	keepalive_count (0),
	publish_count (0),
	confirm_req_count (0),
	confirm_ack_count (0),
	bulk_pull_count (0),
	bulk_push_count (0),
	frontier_req_count (0)
	{
	}
	void keepalive (rai::keepalive const &)
	{
		++keepalive_count;
	}
	void publish (rai::publish const &)
	{
		++publish_count;
	}
	void confirm_req (rai::confirm_req const &)
	{
		++confirm_req_count;
	}
	void confirm_ack (rai::confirm_ack const &)
	{
		++confirm_ack_count;
	}
	void bulk_pull (rai::bulk_pull const &)
	{
		++bulk_pull_count;
	}
	void bulk_push (rai::bulk_push const &)
	{
		++bulk_push_count;
	}
	void frontier_req (rai::frontier_req const &)
	{
		++frontier_req_count;
	}
	uint64_t keepalive_count;
	uint64_t publish_count;
	uint64_t confirm_req_count;
	uint64_t confirm_ack_count;
	uint64_t bulk_pull_count;
	uint64_t bulk_push_count;
	uint64_t frontier_req_count;
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr <rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	auto vote (std::make_shared <rai::vote> (0, rai::keypair ().prv, 0, std::move (block)));
	rai::confirm_ack message (vote);
	std::vector <uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_FALSE (parser.error);
	parser.deserialize_confirm_ack (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_FALSE (parser.error);
	bytes.push_back (0);
	parser.deserialize_confirm_ack (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_TRUE (parser.error);
}

TEST (message_parser, exact_confirm_req_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr <rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	rai::confirm_req message (std::move (block));
	std::vector <uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_FALSE (parser.error);
	parser.deserialize_confirm_req (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_FALSE (parser.error);
	bytes.push_back (0);
	parser.deserialize_confirm_req (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_TRUE (parser.error);
}

TEST (message_parser, exact_publish_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr <rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	rai::publish message (std::move (block));
	std::vector <uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_FALSE (parser.error);
	parser.deserialize_publish (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_FALSE (parser.error);
	bytes.push_back (0);
	parser.deserialize_publish (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_TRUE (parser.error);
}

TEST (message_parser, exact_keepalive_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	rai::keepalive message;
	std::vector <uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_FALSE (parser.error);
	parser.deserialize_keepalive (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_FALSE (parser.error);
	bytes.push_back (0);
	parser.deserialize_keepalive (bytes.data (), bytes.size ());
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_TRUE (parser.error);
}
