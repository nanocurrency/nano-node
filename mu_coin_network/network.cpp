#include <mu_coin_network/network.hpp>
#include <mu_coin_network/messages.hpp>
#include <messages.pb.h>
#include <google/protobuf/io/coded_stream.h>
#include <mu_coin/mu_coin.hpp>

mu_coin_network::node::node (boost::asio::io_service & service_a, uint16_t port) :
socket (service_a, boost::asio::ip::udp::endpoint (boost::asio::ip::udp::v4 (), port)),
service (service_a),
keepalive_req (0),
keepalive_ack (0),
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
    auto message (new mu_coin_network::message_type);
    message->set_type (mu_coin_network::keepalive_req_type);
    auto buffer (new std::string);
    message->SerializeToString (buffer);
    socket.async_send_to (boost::asio::buffer (buffer->c_str (), buffer->size ()), endpoint_a, [this, buffer] (boost::system::error_code const & error, size_t size_a) {delete buffer;});
}

void mu_coin_network::node::receive_action (boost::system::error_code const & error, size_t size_a)
{
    if (!error && on)
    {
        mu_coin_network::message_type type;
        type.ParseFromArray (buffer.data (), size_a);
        mu_coin_network::address address;
        switch (type.type())
        {
            case mu_coin_network::type::keepalive_req_type:
            {
                ++keepalive_req;
                boost::asio::ip::udp::endpoint sender (remote);
                receive ();
                auto message (new mu_coin_network::message_type);
                message->set_type (mu_coin_network::keepalive_ack_type);
                auto buffer (new std::string);
                message->SerializeToString (buffer);
                socket.async_send_to (boost::asio::buffer (buffer->c_str (), buffer->size ()), sender, [this, buffer] (boost::system::error_code const & error, size_t size_a) {delete buffer;});
                break;
            }
            case mu_coin_network::type::keepalive_ack_type:
            {
                ++keepalive_ack;
                receive ();
                break;
            }
            default:
                assert (false);
        }
    }
}