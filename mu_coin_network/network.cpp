#include <mu_coin_network/network.hpp>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <mu_coin/mu_coin.hpp>

mu_coin_network::node::node (boost::asio::io_service & service_a, uint16_t port, mu_coin::ledger & ledger_a) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::udp::v4 (), port)),
service (service_a),
ledger (ledger_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
publish_ack_count (0),
publish_nak_count (0),
unknown_count (0),
on (true)
{
}

void mu_coin_network::node::receive ()
{
    socket.async_receive_from (boost::asio::buffer (buffer), remote, [this] (boost::system::error_code const & error, size_t size_a) {receive_action (error, size_a); });
}

void mu_coin_network::node::stop ()
{
    on = false;
    socket.close ();
}

void mu_coin_network::node::send_keepalive (boost::asio::ip::udp::endpoint const & endpoint_a)
{
    auto message (new mu_coin_network::keepalive_req);
    socket.async_send_to (message->buffers, endpoint_a, [message] (boost::system::error_code const &, size_t) {delete message;});
}

void mu_coin_network::node::send_publish (boost::asio::ip::udp::endpoint const & endpoint_a, std::unique_ptr <mu_coin::transaction_block> block)
{
    auto message (new mu_coin_network::publish_req (std::move (block)));
    socket.async_send_to (message->buffers, endpoint_a, [message] (boost::system::error_code const &, size_t) {delete message;});
}

void mu_coin_network::node::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        if (size_a >= sizeof (uint16_t))
        {
            mu_coin::byte_read_stream type_stream (buffer.data (), size_a);
            uint16_t network_type;
            type_stream.read (network_type);
            mu_coin_network::type type (static_cast <mu_coin_network::type> (ntohs (network_type)));
            switch (type)
            {
                case mu_coin_network::type::keepalive_req:
                {
                    ++keepalive_req_count;
                    auto sender (remote);
                    receive ();
                    auto message (new mu_coin_network::keepalive_ack);
                    socket.async_send_to (message->buffers, sender, [message] (boost::system::error_code const & error, size_t size_a) {delete message;});
                    break;
                }
                case mu_coin_network::type::keepalive_ack:
                {
                    ++keepalive_ack_count;
                    receive ();
                    break;
                }
                case mu_coin_network::type::publish_req:
                {
                    ++publish_req_count;
                    auto sender (remote);
                    receive ();
                    auto incoming (new mu_coin_network::publish_req);
                    mu_coin::byte_read_stream stream (buffer.data (), size_a);
                    auto error (incoming->deserialize (stream));
                    if (!error)
                    {
                        auto process_error (ledger.process (*incoming->block));
                        if (!process_error)
                        {
                            auto outgoing (new (mu_coin_network::publish_ack));
                            socket.async_send_to (outgoing->buffers, sender, [outgoing] (boost::system::error_code const & error, size_t size_a) {delete outgoing;});
                        }
                        else
                        {
                            auto outgoing (new (mu_coin_network::publish_nak));
                            socket.async_send_to (outgoing->buffers, sender, [outgoing] (boost::system::error_code const & error, size_t size_a) {delete outgoing;});
                        }
                    }
                    break;
                }
                case mu_coin_network::type::publish_ack:
                {
                    ++publish_ack_count;
                    break;
                }
                case mu_coin_network::type::publish_nak:
                {
                    ++publish_nak_count;
                    break;
                }
                default:
                {
                    ++unknown_count;
                    receive ();
                    break;
                }
            }
        }
    }
}

mu_coin_network::keepalive_req::keepalive_req () :
type (htons (static_cast <uint16_t> (mu_coin_network::type::keepalive_req)))
{
    buffers [0] = boost::asio::const_buffer (&type, sizeof (type));
}

mu_coin_network::keepalive_ack::keepalive_ack () :
type (htons (static_cast <uint16_t> (mu_coin_network::type::keepalive_ack)))
{
    buffers [0] = boost::asio::const_buffer (&type, sizeof (type));
}

mu_coin_network::publish_req::publish_req (std::unique_ptr <mu_coin::transaction_block> block_a) :
type (htons (static_cast <uint16_t> (mu_coin_network::type::publish_req))),
entry_count (htons (block_a->entries.size ())),
block (std::move (block_a))
{
    build_buffers ();
}

void mu_coin_network::publish_req::build_buffers ()
{
    buffers.reserve (2 + block->entries.size () * 4);
    buffers.push_back (boost::asio::const_buffer (&type, sizeof (type)));
    buffers.push_back (boost::asio::const_buffer (&entry_count, sizeof (entry_count)));
    for (auto & i: block->entries)
    {
        buffers.push_back (boost::asio::const_buffer (i.id.address.point.bytes.data (), sizeof (i.id.address.point.bytes)));
        buffers.push_back (boost::asio::const_buffer (&i.id.sequence, sizeof (i.id.sequence)));
        buffers.push_back (boost::asio::const_buffer (i.signature.bytes.data (), sizeof (i.signature.bytes)));
        buffers.push_back (boost::asio::const_buffer (i.coins.bytes.data (), sizeof (i.coins.bytes)));
    }
}

mu_coin_network::publish_req::publish_req () :
type (htons (static_cast <uint16_t> (mu_coin_network::type::publish_req))),
block (new mu_coin::transaction_block)
{
}

bool mu_coin_network::publish_req::deserialize (mu_coin::byte_read_stream & stream)
{
    auto result (false);
    result = stream.read (type);
    assert (!result);
    result = stream.read (entry_count);
    auto entry_count_l (ntohs (entry_count));
    if (!result)
    {
        block->entries.reserve (entry_count_l);
        for (uint32_t i (0), j (entry_count_l); i < j && !result; ++i)
        {
            block->entries.push_back (mu_coin::entry ());
            auto & back (block->entries.back ());
            result = stream.read (back.id.address.point.bytes);
            if (!result)
            {
                result = back.id.address.point.validate ();
                if (!result)
                {
                    result = stream.read (back.id.sequence);
                    if (!result)
                    {
                        result = stream.read (back.signature.bytes);
                        if (!result)
                        {
                            result = stream.read (back.coins.bytes);
                            if (!result)
                            {
                                build_buffers ();
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

void mu_coin_network::publish_req::serialize (mu_coin::byte_write_stream & stream)
{
    for (auto & i: buffers)
    {
        stream.write (boost::asio::buffer_cast <uint8_t const *> (i), boost::asio::buffer_size (i));
    }
}

mu_coin_network::message::~message ()
{
}

mu_coin_network::publish_ack::publish_ack () :
type (htons (static_cast <uint16_t> (mu_coin_network::type::publish_ack)))
{
    buffers [0] = boost::asio::mutable_buffer (&type, sizeof (type));
}

mu_coin_network::publish_nak::publish_nak () :
type (htons (static_cast <uint16_t> (mu_coin_network::type::publish_nak)))
{
    buffers [0] = boost::asio::mutable_buffer (&type, sizeof (type));
}