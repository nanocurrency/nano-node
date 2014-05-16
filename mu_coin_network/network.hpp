#pragma once

#include <boost/asio.hpp>
#include <array>

namespace mu_coin_network {
    class node
    {
    public:
        node (boost::asio::io_service &, uint16_t);
        void receive ();
        void receive_action (boost::system::error_code const &, size_t);
        void send_keepalive (boost::asio::ip::udp::endpoint const &);
        boost::asio::ip::udp::endpoint remote;
        std::array <uint8_t, 4000> buffer;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        uint64_t keepalive_req;
        uint64_t keepalive_ack;
    };
}