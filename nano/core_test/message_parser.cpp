#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
class dev_visitor : public nano::message_visitor
{
public:
	void keepalive (nano::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (nano::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (nano::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (nano::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (nano::bulk_pull const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_pull_account (nano::bulk_pull_account const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_push (nano::bulk_push const &) override
	{
		ASSERT_FALSE (true);
	}
	void frontier_req (nano::frontier_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void node_id_handshake (nano::node_id_handshake const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_req (nano::telemetry_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_ack (nano::telemetry_ack const &) override
	{
		ASSERT_FALSE (true);
	}

	uint64_t keepalive_count{ 0 };
	uint64_t publish_count{ 0 };
	uint64_t confirm_req_count{ 0 };
	uint64_t confirm_ack_count{ 0 };
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	nano::system system (1);
	dev_visitor visitor;
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);
	nano::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, nano::dev::network_params.network);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	auto vote (std::make_shared<nano::vote> (0, nano::keypair ().prv, 0, 0, std::vector<nano::block_hash>{ block->hash () }));
	nano::confirm_ack message{ nano::dev::network_params.network, vote };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	auto error (false);
	nano::bufferstream stream1 (bytes.data (), bytes.size ());
	nano::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	bytes.push_back (0);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, nano::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	nano::system system (1);
	dev_visitor visitor;
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);
	nano::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, nano::dev::network_params.network);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	nano::confirm_req message{ nano::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	auto error (false);
	nano::bufferstream stream1 (bytes.data (), bytes.size ());
	nano::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	bytes.push_back (0);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, nano::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_hash_size)
{
	nano::system system (1);
	dev_visitor visitor;
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);
	nano::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, nano::dev::network_params.network);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();
	nano::confirm_req message{ nano::dev::network_params.network, block->hash (), block->root () };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	auto error (false);
	nano::bufferstream stream1 (bytes.data (), bytes.size ());
	nano::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	bytes.push_back (0);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, nano::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	nano::system system (1);
	dev_visitor visitor;
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);
	nano::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, nano::dev::network_params.network);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build_shared ();
	nano::publish message{ nano::dev::network_params.network, block };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	auto error (false);
	nano::bufferstream stream1 (bytes.data (), bytes.size ());
	nano::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	bytes.push_back (0);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, nano::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	nano::system system (1);
	dev_visitor visitor;
	nano::network_filter filter (1);
	nano::block_uniquer block_uniquer;
	nano::vote_uniquer vote_uniquer (block_uniquer);
	nano::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work, nano::dev::network_params.network);
	nano::keepalive message{ nano::dev::network_params.network };
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	auto error (false);
	nano::bufferstream stream1 (bytes.data (), bytes.size ());
	nano::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, nano::message_parser::parse_status::success);
	bytes.push_back (0);
	nano::bufferstream stream2 (bytes.data (), bytes.size ());
	nano::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, nano::message_parser::parse_status::success);
}
