#pragma once

#include <nano/boost/asio/ip/tcp.hpp>

namespace nano
{
using endpoint = boost::asio::ip::tcp::endpoint;
using tcp_endpoint = endpoint;
}