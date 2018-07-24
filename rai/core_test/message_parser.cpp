#include <gtest/gtest.h>
#include <rai/node/testing.hpp>

namespace
{
class test_visitor : public rai::message_visitor
{
public:
	test_visitor () :
	keepalive_count (0),
	publish_count (0),
	confirm_req_count (0),
	confirm_ack_count (0),
	bulk_pull_count (0),
	bulk_pull_account_count (0),
	bulk_pull_blocks_count (0),
	bulk_push_count (0),
	frontier_req_count (0)
	{
	}
	void keepalive (rai::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (rai::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (rai::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (rai::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (rai::bulk_pull const &) override
	{
		++bulk_pull_count;
	}
	void bulk_pull_account (rai::bulk_pull_account const &) override
	{
		++bulk_pull_account_count;
	}
	void bulk_pull_blocks (rai::bulk_pull_blocks const &) override
	{
		++bulk_pull_blocks_count;
	}
	void bulk_push (rai::bulk_push const &) override
	{
		++bulk_push_count;
	}
	void frontier_req (rai::frontier_req const &) override
	{
		++frontier_req_count;
	}
	void node_id_handshake (rai::node_id_handshake const &) override
	{
		++node_id_handshake_count;
	}
	void musig_stage0_req (rai::musig_stage0_req const &) override
	{
		++musig_stage0_req_count;
	}
	void musig_stage0_res (rai::musig_stage0_res const &) override
	{
		++musig_stage0_res_count;
	}
	void musig_stage1_req (rai::musig_stage1_req const &) override
	{
		++musig_stage1_req_count;
	}
	void musig_stage1_res (rai::musig_stage1_res const &) override
	{
		++musig_stage1_res_count;
	}
	uint64_t keepalive_count;
	uint64_t publish_count;
	uint64_t confirm_req_count;
	uint64_t confirm_ack_count;
	uint64_t bulk_pull_count;
	uint64_t bulk_pull_account_count;
	uint64_t bulk_pull_blocks_count;
	uint64_t bulk_push_count;
	uint64_t frontier_req_count;
	uint64_t node_id_handshake_count;
	uint64_t musig_stage0_req_count;
	uint64_t musig_stage0_res_count;
	uint64_t musig_stage1_req_count;
	uint64_t musig_stage1_res_count;
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	auto vote (std::make_shared<rai::vote> (0, rai::keypair ().prv, 0, std::move (block)));
	rai::confirm_ack message (vote);
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	auto error (false);
	rai::bufferstream stream1 (bytes.data (), bytes.size ());
	rai::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	bytes.push_back (0);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, rai::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	rai::confirm_req message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	auto error (false);
	rai::bufferstream stream1 (bytes.data (), bytes.size ());
	rai::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	bytes.push_back (0);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, rai::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	auto block (std::unique_ptr<rai::send_block> (new rai::send_block (1, 1, 2, rai::keypair ().prv, 4, system.work.generate (1))));
	rai::publish message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	auto error (false);
	rai::bufferstream stream1 (bytes.data (), bytes.size ());
	rai::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	bytes.push_back (0);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, rai::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	rai::system system (24000, 1);
	test_visitor visitor;
	rai::message_parser parser (visitor, system.work);
	rai::keepalive message;
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	auto error (false);
	rai::bufferstream stream1 (bytes.data (), bytes.size ());
	rai::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, rai::message_parser::parse_status::success);
	bytes.push_back (0);
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	rai::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, rai::message_parser::parse_status::success);
}
