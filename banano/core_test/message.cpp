#include <gtest/gtest.h>

#include <banano/node/common.hpp>

TEST (message, keepalive_serialization)
{
	rai::keepalive request1;
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		request1.serialize (stream);
	}
	auto error (false);
	rai::bufferstream stream (bytes.data (), bytes.size ());
	rai::message_header header (error, stream);
	ASSERT_FALSE (error);
	rai::keepalive request2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
	rai::keepalive message1;
	message1.peers[0] = rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		message1.serialize (stream);
	}
	rai::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	rai::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::message_type::keepalive, header.type);
	rai::keepalive message2 (error, stream, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (message1.peers, message2.peers);
}

TEST (message, publish_serialization)
{
	rai::publish publish (std::unique_ptr<rai::block> (new rai::send_block (0, 1, 2, rai::keypair ().prv, 4, 5)));
	ASSERT_EQ (rai::block_type::send, publish.header.block_type ());
	ASSERT_FALSE (publish.header.ipv4_only ());
	publish.header.ipv4_only_set (true);
	ASSERT_TRUE (publish.header.ipv4_only ());
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream (bytes);
		publish.header.serialize (stream);
	}
	ASSERT_EQ (8, bytes.size ());
	ASSERT_EQ (0x52, bytes[0]);
	ASSERT_EQ (0x41, bytes[1]);
	ASSERT_EQ (rai::protocol_version, bytes[2]);
	ASSERT_EQ (rai::protocol_version, bytes[3]);
	ASSERT_EQ (rai::protocol_version_min, bytes[4]);
	ASSERT_EQ (static_cast<uint8_t> (rai::message_type::publish), bytes[5]);
	ASSERT_EQ (0x02, bytes[6]);
	ASSERT_EQ (static_cast<uint8_t> (rai::block_type::send), bytes[7]);
	rai::bufferstream stream (bytes.data (), bytes.size ());
	auto error (false);
	rai::message_header header (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (rai::protocol_version_min, header.version_min);
	ASSERT_EQ (rai::protocol_version, header.version_using);
	ASSERT_EQ (rai::protocol_version, header.version_max);
	ASSERT_EQ (rai::message_type::publish, header.type);
}

TEST (message, confirm_ack_serialization)
{
	rai::keypair key1;
	auto vote (std::make_shared<rai::vote> (key1.pub, key1.prv, 0, std::unique_ptr<rai::block> (new rai::send_block (0, 1, 2, key1.prv, 4, 5))));
	rai::confirm_ack con1 (vote);
	std::vector<uint8_t> bytes;
	{
		rai::vectorstream stream1 (bytes);
		con1.serialize (stream1);
	}
	rai::bufferstream stream2 (bytes.data (), bytes.size ());
	bool error (false);
	rai::message_header header (error, stream2);
	rai::confirm_ack con2 (error, stream2, header);
	ASSERT_FALSE (error);
	ASSERT_EQ (con1, con2);
}
