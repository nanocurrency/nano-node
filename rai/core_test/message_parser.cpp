#include <gtest/gtest.h>
#include <rai/core/core.hpp>

TEST (message_parser, exact_confirm_unk_size)
{
    rai::system system (24000, 1);
    rai::confirm_unk message;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message.serialize (stream);
    }
    ASSERT_EQ (0, system.clients [0]->network.parser.confirm_unk_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    system.clients [0]->network.parser.deserialize_confirm_unk (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_EQ (1, system.clients [0]->network.parser.confirm_unk_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    bytes.push_back (0);
    system.clients [0]->network.parser.deserialize_confirm_unk (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_EQ (1, system.clients [0]->network.parser.confirm_unk_count);
    ASSERT_EQ (1, system.clients [0]->network.parser.error_count);
}

TEST (message_parser, exact_confirm_ack_size)
{
    rai::system system (24000, 1);
    rai::confirm_ack message (std::unique_ptr <rai::send_block> (new rai::send_block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message.serialize (stream);
    }
    ASSERT_EQ (0, system.clients [0]->network.parser.confirm_ack_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    system.clients [0]->network.parser.deserialize_confirm_ack (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.confirm_ack_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    bytes.push_back (0);
    system.clients [0]->network.parser.deserialize_confirm_ack (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.confirm_ack_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (1, system.clients [0]->network.parser.error_count);
}

TEST (message_parser, exact_confirm_req_size)
{
    rai::system system (24000, 1);
    rai::confirm_req message (std::unique_ptr <rai::send_block> (new rai::send_block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message.serialize (stream);
    }
    ASSERT_EQ (0, system.clients [0]->network.parser.confirm_req_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    system.clients [0]->network.parser.deserialize_confirm_req (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.confirm_req_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    bytes.push_back (0);
    system.clients [0]->network.parser.deserialize_confirm_req (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.confirm_req_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (1, system.clients [0]->network.parser.error_count);
}

TEST (message_parser, exact_publish_size)
{
    rai::system system (24000, 1);
	auto block (std::unique_ptr <rai::send_block> (new rai::send_block));
	block->hashables.previous = 1;
	rai::publish message (std::move (block));
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message.serialize (stream);
    }
    ASSERT_EQ (0, system.clients [0]->network.parser.publish_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    system.clients [0]->network.parser.deserialize_publish (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.publish_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    bytes.push_back (0);
    system.clients [0]->network.parser.deserialize_publish (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_TRUE ((system.clients [0]->network.parser.publish_count == 1) != (system.clients [0]->network.parser.work.insufficient_work_count == 1));
    ASSERT_EQ (1, system.clients [0]->network.parser.error_count);
}

TEST (message_parser, exact_keepalive_size)
{
    rai::system system (24000, 1);
    rai::keepalive message;
    std::vector <uint8_t> bytes;
    {
        rai::vectorstream stream (bytes);
        message.serialize (stream);
    }
    ASSERT_EQ (0, system.clients [0]->network.parser.keepalive_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    system.clients [0]->network.parser.deserialize_keepalive (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_EQ (1, system.clients [0]->network.parser.keepalive_count);
    ASSERT_EQ (0, system.clients [0]->network.parser.error_count);
    bytes.push_back (0);
    system.clients [0]->network.parser.deserialize_keepalive (bytes.data (), bytes.size (), rai::endpoint ());
    ASSERT_EQ (1, system.clients [0]->network.parser.keepalive_count);
    ASSERT_EQ (1, system.clients [0]->network.parser.error_count);
}