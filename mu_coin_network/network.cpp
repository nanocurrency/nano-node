#include <mu_coin_network/network.hpp>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <mu_coin/mu_coin.hpp>

mu_coin_network::node::node (boost::asio::io_service & service_a, uint16_t port) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::udp::v4 (), port)),
service (service_a),
keepalive_req_count (0),
keepalive_ack_count (0),
publish_req_count (0),
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
    send_keepalive (socket.local_endpoint ());
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
            mu_coin::byte_read_stream stream (buffer.data (), buffer.size ());
            uint16_t network_type;
            stream.read (network_type);
            mu_coin_network::type type (static_cast <mu_coin_network::type> (ntohs (network_type)));
            switch (type)
            {
                case mu_coin_network::type::keepalive_req:
                {
                    ++keepalive_req_count;
                    boost::asio::ip::udp::endpoint sender (remote);
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
                    receive ();
                }
                default:
                    ++unknown_count;
                    receive ();
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
entry_count (htonl (block_a->entries.size ())),
block (std::move (block_a))
{
    buffers.reserve (2 + block_a->entries.size () * 4);
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
block (new mu_coin::transaction_block)
{
}

bool mu_coin_network::publish_req::deserialize (mu_coin::byte_read_stream & stream)
{
    
}

mu_coin_network::message::~message ()
{
}