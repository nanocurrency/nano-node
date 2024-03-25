#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/common.hpp>
#include <nano/node/messages.hpp>
#include <nano/node/transport/socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace nano::transport
{
nano::endpoint map_endpoint_to_v6 (nano::endpoint const &);
nano::endpoint map_tcp_to_endpoint (nano::tcp_endpoint const &);
nano::tcp_endpoint map_endpoint_to_tcp (nano::endpoint const &);
boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address);
boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address);
boost::asio::ip::address_v6 mapped_from_v4_bytes (unsigned long);
boost::asio::ip::address_v6 mapped_from_v4_or_v6 (boost::asio::ip::address const &);
bool is_ipv4_or_v4_mapped_address (boost::asio::ip::address const &);
bool is_same_ip (boost::asio::ip::address const &, boost::asio::ip::address const &);
bool is_same_subnetwork (boost::asio::ip::address const &, boost::asio::ip::address const &);

// Unassigned, reserved, self
bool reserved_address (nano::endpoint const &, bool allow_local_peers = false);

bool is_temporary_error (boost::system::error_code const &);
}