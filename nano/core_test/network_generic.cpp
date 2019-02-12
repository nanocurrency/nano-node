#include <boost/thread.hpp>
#include <gtest/gtest.h>
#include <map>
#include <nano/core_test/testutil.hpp>
#include <nano/node/network_generic.hpp>
#include <nano/node/testing.hpp>
#include <set>
#include <unordered_map>

using namespace std::chrono_literals;

TEST (net, remote_protocol_predicates)
{
	bool error;

	auto tcp1 (nano::net::socket_addr::make_tcp ("::1:24000", error));
	ASSERT_TRUE (tcp1.is_tcp ());
	ASSERT_FALSE (tcp1.is_udp ());

	auto udp1 (nano::net::socket_addr::make_udp ("::1:24000", error));
	ASSERT_TRUE (udp1.is_udp ());
	ASSERT_FALSE (udp1.is_tcp ());
}

TEST (net, remote_parse_relational)
{
	bool error;
	auto tcp1 (nano::net::socket_addr::make_tcp ("::1:24000", error));
	auto tcp2 (nano::net::socket_addr::make_tcp ("::1:24000", error));
	auto tcp3 (nano::net::socket_addr::make_tcp ("::1:25000", error));
	auto udp1 (nano::net::socket_addr::make_udp ("::1:24000", error));
	ASSERT_EQ (tcp1, tcp2);
	ASSERT_NE (tcp1, tcp3);
	ASSERT_LT (tcp1, tcp3);
	ASSERT_GT (tcp3, tcp2);
	// TCP sorts before UDP
	ASSERT_LT (tcp1, udp1);
}

TEST (net, remote_container)
{
	bool error;
	// Make sure duplicate addresses are treated as such and that
	// the same address for udp and tcp are treated as different.
	std::set<nano::net::socket_addr> remotes;
	remotes.insert (nano::net::socket_addr::make_tcp ("::1:24000", error));
	remotes.insert (nano::net::socket_addr::make_tcp ("::1:24000", error));
	remotes.insert (nano::net::socket_addr::make_udp ("::ffff:192.168.40.2:24000", error));
	remotes.insert (nano::net::socket_addr::make_udp ("::ffff:192.168.40.1:24000", error));
	remotes.insert (nano::net::socket_addr::make_tcp ("::ffff:192.168.40.1:24000", error));
	remotes.insert (nano::net::socket_addr::make_udp ("::ffff:192.168.40.1:25000", error));
	ASSERT_EQ (remotes.size (), 5);

	// Add same socket address twice for different types, make sure tcp is sorted first
	remotes.clear ();
	remotes.insert (nano::net::socket_addr::make_udp ("::ffff:192.168.40.1:24000", error));
	remotes.insert (nano::net::socket_addr::make_tcp ("::ffff:192.168.40.1:24000", error));
	ASSERT_TRUE (remotes.begin ()->is_tcp ());
	ASSERT_TRUE (remotes.rbegin ()->is_udp ());

	// Test the hash
	std::unordered_map<nano::net::socket_addr, std::string> map;
	map[nano::net::socket_addr::make_tcp ("::1:24000", error)] = "a";
	map[nano::net::socket_addr::make_tcp ("::1:24001", error)] = "b";
	map[nano::net::socket_addr::make_tcp ("::1:24002", error)] = "c";
	map[nano::net::socket_addr::make_tcp ("::ffff:192.168.40.1:25000", error)] = "d";
	map[nano::net::socket_addr::make_tcp ("::ffff:192.168.40.2:25000", error)] = "e";
}
