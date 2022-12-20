#pragma once

#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/ip/udp.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/asio.hpp>
#include <nano/lib/jsonconfig.hpp>
#include <nano/lib/memory.hpp>
#include <nano/lib/stats.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/network_filter.hpp>

#include <bitset>

namespace nano
{
using endpoint = boost::asio::ip::udp::endpoint;
bool parse_port (std::string const &, uint16_t &);
bool parse_address (std::string const &, boost::asio::ip::address &);
bool parse_address_port (std::string const &, boost::asio::ip::address &, uint16_t &);
using tcp_endpoint = boost::asio::ip::tcp::endpoint;
bool parse_endpoint (std::string const &, nano::endpoint &);
bool parse_tcp_endpoint (std::string const &, nano::tcp_endpoint &);
uint64_t ip_address_hash_raw (boost::asio::ip::address const & ip_a, uint16_t port = 0);
}

namespace
{
uint64_t endpoint_hash_raw (nano::endpoint const & endpoint_a)
{
	uint64_t result (nano::ip_address_hash_raw (endpoint_a.address (), endpoint_a.port ()));
	return result;
}
uint64_t endpoint_hash_raw (nano::tcp_endpoint const & endpoint_a)
{
	uint64_t result (nano::ip_address_hash_raw (endpoint_a.address (), endpoint_a.port ()));
	return result;
}

template <std::size_t size>
struct endpoint_hash
{
};

template <>
struct endpoint_hash<8>
{
	std::size_t operator() (nano::endpoint const & endpoint_a) const
	{
		return endpoint_hash_raw (endpoint_a);
	}
	std::size_t operator() (nano::tcp_endpoint const & endpoint_a) const
	{
		return endpoint_hash_raw (endpoint_a);
	}
};

template <>
struct endpoint_hash<4>
{
	std::size_t operator() (nano::endpoint const & endpoint_a) const
	{
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
	std::size_t operator() (nano::tcp_endpoint const & endpoint_a) const
	{
		uint64_t big (endpoint_hash_raw (endpoint_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};

template <std::size_t size>
struct ip_address_hash
{
};

template <>
struct ip_address_hash<8>
{
	std::size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		return nano::ip_address_hash_raw (ip_address_a);
	}
};

template <>
struct ip_address_hash<4>
{
	std::size_t operator() (boost::asio::ip::address const & ip_address_a) const
	{
		uint64_t big (nano::ip_address_hash_raw (ip_address_a));
		uint32_t result (static_cast<uint32_t> (big) ^ static_cast<uint32_t> (big >> 32));
		return result;
	}
};
}

namespace std
{
template <>
struct hash<::nano::endpoint>
{
	std::size_t operator() (::nano::endpoint const & endpoint_a) const
	{
		endpoint_hash<sizeof (std::size_t)> ehash;
		return ehash (endpoint_a);
	}
};

template <>
struct hash<::nano::tcp_endpoint>
{
	std::size_t operator() (::nano::tcp_endpoint const & endpoint_a) const
	{
		endpoint_hash<sizeof (std::size_t)> ehash;
		return ehash (endpoint_a);
	}
};

#ifndef BOOST_ASIO_HAS_STD_HASH
template <>
struct hash<boost::asio::ip::address>
{
	std::size_t operator() (boost::asio::ip::address const & ip_a) const
	{
		ip_address_hash<sizeof (std::size_t)> ihash;
		return ihash (ip_a);
	}
};
#endif
}

namespace boost
{
template <>
struct hash<::nano::endpoint>
{
	std::size_t operator() (::nano::endpoint const & endpoint_a) const
	{
		std::hash<::nano::endpoint> hash;
		return hash (endpoint_a);
	}
};

template <>
struct hash<::nano::tcp_endpoint>
{
	std::size_t operator() (::nano::tcp_endpoint const & endpoint_a) const
	{
		std::hash<::nano::tcp_endpoint> hash;
		return hash (endpoint_a);
	}
};

template <>
struct hash<boost::asio::ip::address>
{
	std::size_t operator() (boost::asio::ip::address const & ip_a) const
	{
		std::hash<boost::asio::ip::address> hash;
		return hash (ip_a);
	}
};
}

namespace nano
{
/** Helper guard which contains all the necessary purge (remove all memory even if used) functions */
class node_singleton_memory_pool_purge_guard
{
public:
	node_singleton_memory_pool_purge_guard ();

private:
	nano::cleanup_guard cleanup_guard;
};
}
