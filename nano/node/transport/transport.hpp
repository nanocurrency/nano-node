#pragma once

#include <celerix/lib/locks.hpp>
#include <celerix/lib/stats.hpp>
#include <celerix/node/bandwidth_limiter.hpp>
#include <celerix/node/endpoint.hpp>
#include <celerix/node/messages.hpp>
#include <celerix/node/transport/tcp_socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace celerix::transport
{
celerix::endpoint map_endpoint_to_v6 (celerix::endpoint const &);
celerix::endpoint map_tcp_to_endpoint (celerix::tcp_endpoint const &);
celerix::tcp_endpoint map_endpoint_to_tcp (celerix::endpoint const &);
boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address);
boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address);
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long);
boost::asio::ip::address_v6 mapped_from_v4_or_v6 (boost::asio::ip::address const &);
bool is_ipv4_or_v4_mapped_address (boost::asio::ip::address const &);
bool is_same_ip (boost::asio::ip::address const &, boost::asio::ip::address const &);
bool is_same_subnetwork (boost::asio::ip::address const &, boost::asio::ip::address const &);

// Unassigned, reserved, self
bool reserved_address (celerix::endpoint const &, bool allow_local_peers = false);

using address_socket_mmap = std::multimap<boost::asio::ip::address, std::weak_ptr<tcp_socket>>;

namespace socket_functions
{
	boost::asio::ip::network_v6 get_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address first_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	boost::asio::ip::address last_ipv6_subnet_address (boost::asio::ip::address_v6 const &, std::size_t);
	std::size_t count_subnetwork_connections (celerix::transport::address_socket_mmap const &, boost::asio::ip::address_v6 const &, std::size_t);
}
}

namespace celerix
{
celerix::stat::detail to_stat_detail (boost::system::error_code const &);
}