#pragma once

namespace boost::asio::ip
{
class tcp;
template <typename InternetProtocol>
class basic_endpoint;
}

namespace celerix
{
using endpoint = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>;
using tcp_endpoint = endpoint;
}
