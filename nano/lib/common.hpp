#pragma once

namespace boost::asio::ip
{
class tcp;
template <typename InternetProtocol>
class basic_endpoint;
}

namespace nano
{
using endpoint = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>;
using tcp_endpoint = endpoint;
}
