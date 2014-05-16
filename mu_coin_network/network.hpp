#pragma once

#include <functional>
#include <boost/asio.hpp>
#include <array>

namespace mu_coin
{
    class transaction_block;
    class byte_read_stream;
}
namespace mu_coin_network {
    enum class type : uint16_t
    {
        keepalive_req,
        keepalive_ack,
        publish_req
    };
    class message
    {
    public:
        virtual ~message ();
    };
    class keepalive_req : public message
    {
    public:
        keepalive_req ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class keepalive_ack : public message
    {
    public:
        keepalive_ack ();
        std::array <boost::asio::const_buffer, 1> buffers;
        uint16_t type;
    };
    class publish_req : public message
    {
    public:
        publish_req ();
        publish_req (std::unique_ptr <mu_coin::transaction_block>);
        bool deserialize (mu_coin::byte_read_stream &);
        std::vector <boost::asio::const_buffer> buffers;
        uint16_t type;
        uint32_t entry_count;
        std::unique_ptr <mu_coin::transaction_block> block;
    };
    class node
    {
    public:
        node (boost::asio::io_service &, uint16_t);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void send_keepalive (boost::asio::ip::udp::endpoint const &);
        void send_publish (boost::asio::ip::udp::endpoint const &, std::unique_ptr <mu_coin::transaction_block>);
        boost::asio::ip::udp::endpoint remote;
        std::array <uint8_t, 4000> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t unknown_count;
        bool on;
    };
}