#include <nano/lib/stream.hpp>
#include <nano/node/endpoint.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

TEST (network_functions, reserved_address)
{
	// 0 port test
	ASSERT_TRUE (nano::transport::reserved_address (nano::endpoint (boost::asio::ip::make_address_v6 ("2001::"), 0)));
	// Valid address test
	ASSERT_FALSE (nano::transport::reserved_address (nano::endpoint (boost::asio::ip::make_address_v6 ("2001::"), 1)));
	nano::endpoint loopback (boost::asio::ip::make_address_v6 ("::1"), 1);
	ASSERT_FALSE (nano::transport::reserved_address (loopback));
	nano::endpoint private_network_peer (boost::asio::ip::make_address_v6 ("::ffff:10.0.0.0"), 1);
	ASSERT_TRUE (nano::transport::reserved_address (private_network_peer, false));
	ASSERT_FALSE (nano::transport::reserved_address (private_network_peer, true));
}

TEST (network_functions, ipv6_bind_subnetwork)
{
	auto address1 (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 48));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298::"), subnet1.network ());
	auto address1_subnet (nano::transport::ipv4_address_or_ipv6_subnet (address1));
	ASSERT_EQ (subnet1.network (), address1_subnet);
	// Ipv4 should return initial address
	auto address2 (boost::asio::ip::make_address_v6 ("::ffff:192.168.1.1"));
	auto address2_subnet (nano::transport::ipv4_address_or_ipv6_subnet (address2));
	ASSERT_EQ (address2, address2_subnet);
}

TEST (network_functions, network_range_ipv6)
{
	auto address1 (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf45:672e:bd1a:e7fb:f713"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 58));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a41d:b7b2:8298:cf40::"), subnet1.network ());
	auto address2 (boost::asio::ip::make_address_v6 ("520d:2402:3d:5e65:11:f8:7c54:3f"));
	auto subnet2 (boost::asio::ip::make_network_v6 (address2, 33));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("520d:2402:0::"), subnet2.network ());
	// Default settings test
	auto address3 (boost::asio::ip::make_address_v6 ("a719:0f12:536e:d88a:1331:ba53:4598:04e5"));
	auto subnet3 (boost::asio::ip::make_network_v6 (address3, 32));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("a719:0f12::"), subnet3.network ());
	auto address3_subnet (nano::transport::map_address_to_subnetwork (address3));
	ASSERT_EQ (subnet3.network (), address3_subnet);
}

TEST (network_functions, network_range_ipv4)
{
	auto address1 (boost::asio::ip::make_address_v6 ("::ffff:192.168.1.1"));
	auto subnet1 (boost::asio::ip::make_network_v6 (address1, 96 + 16));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("::ffff:192.168.0.0"), subnet1.network ());
	// Default settings test
	auto address2 (boost::asio::ip::make_address_v6 ("::ffff:80.67.148.225"));
	auto subnet2 (boost::asio::ip::make_network_v6 (address2, 96 + 24));
	ASSERT_EQ (boost::asio::ip::make_address_v6 ("::ffff:80.67.148.0"), subnet2.network ());
	auto address2_subnet (nano::transport::map_address_to_subnetwork (address2));
	ASSERT_EQ (subnet2.network (), address2_subnet);
}

TEST (network_functions, ipv4_address_or_ipv6_subnet)
{
	// IPv4 mapped as IPv6 address should return the original IPv4 address
	boost::asio::ip::address addr1 = boost::asio::ip::make_address ("192.168.1.1");
	boost::asio::ip::address addr2 = boost::asio::ip::make_address ("::ffff:192.168.1.1");
	ASSERT_EQ (nano::transport::ipv4_address_or_ipv6_subnet (addr1), addr2);

	// IPv6 address within the same /48 subnet should return the network prefix
	boost::asio::ip::address addr3 = boost::asio::ip::make_address ("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
	boost::asio::ip::address addr4 = boost::asio::ip::make_address ("2001:0db8:85a3::");
	ASSERT_EQ (nano::transport::ipv4_address_or_ipv6_subnet (addr3), addr4);

	// Different IPv6 address outside the /48 subnet should not match
	boost::asio::ip::address addr5 = boost::asio::ip::make_address ("2001:0db8:85a4:0001:0000:8a2e:0370:7334");
	ASSERT_NE (nano::transport::ipv4_address_or_ipv6_subnet (addr3), nano::transport::ipv4_address_or_ipv6_subnet (addr5));
}

TEST (network_functions, is_same_ip)
{
	// Same IPv4 addresses
	boost::asio::ip::address ipv4_addr1 = boost::asio::ip::make_address ("192.168.1.1");
	ASSERT_TRUE (nano::transport::is_same_ip (ipv4_addr1, ipv4_addr1));

	// IPv4 and its IPv6 mapped form
	boost::asio::ip::address ipv6_mapped_ipv4 = boost::asio::ip::make_address ("::ffff:192.168.1.1");
	ASSERT_TRUE (nano::transport::is_same_ip (ipv4_addr1, ipv6_mapped_ipv4));
}

TEST (network_functions, is_same_ipv4)
{
	// Same IPv4 addresses
	boost::asio::ip::address ipv4_addr1 = boost::asio::ip::make_address ("192.168.1.1");
	ASSERT_TRUE (nano::transport::is_same_ip (ipv4_addr1, ipv4_addr1));

	// IPv4 and its IPv6 mapped form
	boost::asio::ip::address ipv6_mapped_ipv4 = boost::asio::ip::make_address ("::ffff:192.168.1.1");
	ASSERT_TRUE (nano::transport::is_same_ip (ipv4_addr1, ipv6_mapped_ipv4));
}

TEST (network_functions, is_same_ipv6)
{
	// Two different IPv6 addresses within the same /48 subnet
	boost::asio::ip::address ipv6_addr1 = boost::asio::ip::make_address ("2001:db8::1");
	boost::asio::ip::address ipv6_addr2 = boost::asio::ip::make_address ("2001:db8::2");
	ASSERT_TRUE (nano::transport::is_same_ip (ipv6_addr1, ipv6_addr2));

	// Two different IPv6 addresses in different /48 subnets
	boost::asio::ip::address ipv6_addr3 = boost::asio::ip::make_address ("2001:db8:1::1");
	ASSERT_FALSE (nano::transport::is_same_ip (ipv6_addr1, ipv6_addr3));
}

TEST (network_functions, is_different_ip_family)
{
	boost::asio::ip::address addr1 = boost::asio::ip::make_address ("192.168.1.1");
	boost::asio::ip::address addr2 = boost::asio::ip::make_address ("::1");
	ASSERT_FALSE (nano::transport::is_same_ip (addr1, addr2));
}

TEST (network_functions, is_same_ip_v4_mapped)
{
	boost::asio::ip::address addr1 = boost::asio::ip::make_address ("::ffff:192.168.1.1");
	boost::asio::ip::address addr2 = boost::asio::ip::make_address ("192.168.1.1");
	ASSERT_TRUE (nano::transport::is_same_ip (addr1, addr2));

	boost::asio::ip::address addr3 = boost::asio::ip::make_address ("10.0.0.1");
	ASSERT_FALSE (nano::transport::is_same_ip (addr1, addr3));
}

TEST (network_functions, map_ipv4_address_to_subnetwork)
{
	boost::asio::ip::address addr = boost::asio::ip::make_address ("192.168.1.100");
	auto subnetwork = nano::transport::map_address_to_subnetwork (addr);
	// Assuming a /24 subnet mask for IPv4, all addresses in 192.168.1.x should map to the same network
	// Automatically maps to IPv6
	ASSERT_EQ (subnetwork.to_string (), "::ffff:192.168.1.0");
}

TEST (network_functions, map_ipv6_address_to_subnetwork)
{
	boost::asio::ip::address addr = boost::asio::ip::make_address ("2001:db8:abcd:0012::0");
	auto subnetwork = nano::transport::map_address_to_subnetwork (addr);
	// Assuming a /32 subnet mask for IPv6
	ASSERT_EQ (subnetwork.to_string (), "2001:db8::");
}

/*
 * Address and port split at the last colon, and a false result means the parse succeeded
 */
TEST (parse_endpoint, valid)
{
	nano::endpoint endpoint;
	ASSERT_FALSE (nano::parse_endpoint ("::1:24000", endpoint));
	ASSERT_EQ (boost::asio::ip::address_v6::loopback (), endpoint.address ());
	ASSERT_EQ (24000, endpoint.port ());
}

/*
 * A port that is not a number fails the parse
 */
TEST (parse_endpoint, invalid_port)
{
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint ("::1:24a00", endpoint));
}

/*
 * An address that is not an IPv6 address fails the parse
 */
TEST (parse_endpoint, invalid_address)
{
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint ("::q:24000", endpoint));
}

/*
 * An empty address before the separator fails the parse
 */
TEST (parse_endpoint, no_address)
{
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint (":24000", endpoint));
}

/*
 * An empty port after the separator fails the parse
 */
TEST (parse_endpoint, no_port)
{
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint ("::1:", endpoint));
}

/*
 * An address alone fails, since its last colon is taken for the separator
 */
TEST (parse_endpoint, no_colon)
{
	nano::endpoint endpoint;
	ASSERT_TRUE (nano::parse_endpoint ("::1", endpoint));
}

/*
 * A v4-mapped address serializes as the sixteen bytes of its IPv6 form, ten zeros, two 0xff and the four IPv4 bytes, and reads back into the same endpoint
 */
TEST (network_functions, ipv6)
{
	auto const address = boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1");
	ASSERT_TRUE (address.is_v4_mapped ());
	nano::endpoint const endpoint (address, 16384);

	// The bytes written are the IPv6 layout of the mapped address
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		nano::write (stream, address.to_bytes ());
	}
	ASSERT_EQ (16, bytes.size ());
	ASSERT_TRUE (std::all_of (bytes.begin (), bytes.begin () + 10, [] (uint8_t byte) { return byte == 0; }));
	ASSERT_EQ (0xff, bytes[10]);
	ASSERT_EQ (0xff, bytes[11]);
	ASSERT_EQ ((std::vector<uint8_t>{ 127, 0, 0, 1 }), std::vector<uint8_t> (bytes.begin () + 12, bytes.end ()));

	// Reading them back gives the endpoint that was written
	std::array<uint8_t, 16> read;
	nano::bufferstream stream (bytes.data (), bytes.size ());
	ASSERT_FALSE (nano::try_read (stream, read));
	ASSERT_EQ (endpoint, nano::endpoint (boost::asio::ip::address_v6 (read), 16384));
}
