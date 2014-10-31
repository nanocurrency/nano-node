#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (message, keepalive_serialization)
{
    rai::keepalive request1;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        request1.serialize (stream);
    }
    rai::keepalive request2;
    rai::bufferstream buffer (bytes.data (), bytes.size ());
    ASSERT_FALSE (request2.deserialize (buffer));
    ASSERT_EQ (request1, request2);
}

TEST (message, keepalive_deserialize)
{
    rai::keepalive message1;
    message1.peers [0] = rai::endpoint (boost::asio::ip::address_v6::loopback (), 10000);
    message1.checksum = 1;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message1.serialize (stream);
    }
    uint8_t version_max;
    uint8_t version_using;
    uint8_t version_min;
	rai::message_type type;
	std::bitset <64> extensions;
    rai::bufferstream header_stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (rai::message::read_header (header_stream, version_max, version_using, version_min, type, extensions));
    ASSERT_EQ (rai::message_type::keepalive, type);
    rai::keepalive message2;
    rai::bufferstream stream (bytes.data (), bytes.size ());
    ASSERT_FALSE (message2.deserialize (stream));
    ASSERT_EQ (message1.peers, message2.peers);
}