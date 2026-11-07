#include <nano/node/transport/tcp_listener.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string_view>

using namespace std::chrono_literals;

namespace
{
using nano::transport::connection_type;
using nano::transport::tcp_listener_index;
using result = tcp_listener_index::result;

// Addresses are given as the listener sees them, IPv6 or IPv4 mapped into IPv6
nano::tcp_endpoint endpoint (std::string_view address, uint16_t port = 7075)
{
	return { boost::asio::ip::make_address (address), port };
}

// Selects the entries of one endpoint, connections and attempts alike
auto to (nano::tcp_endpoint const & endpoint)
{
	return [endpoint] (auto const & entry) {
		return entry.endpoint == endpoint;
	};
}

// Injected time starts at the clock's epoch, every test moves forward from here
std::chrono::steady_clock::time_point const origin{};
}

/*
 * Each direction has a cap of its own: with room for one inbound and one outbound connection, a second of either kind is refused while the first is open and allowed again once it has been erased
 */
TEST (tcp_listener_index, per_type)
{
	tcp_listener_index::limits limits;
	limits.max_inbound = 1;
	limits.max_outbound = 1;
	limits.max_per_ip.reset ();
	limits.max_per_subnetwork.reset ();
	limits.reconnect_cooldown = 0s;
	tcp_listener_index index{ limits };
	auto const peer1 = endpoint ("::ffff:10.0.0.1");
	auto const peer2 = endpoint ("::ffff:10.0.0.2");

	// Nothing is recorded, both directions have room
	ASSERT_EQ (index.check (connection_type::inbound, peer1.address ()), result::accepted);
	ASSERT_EQ (index.check (connection_type::outbound, peer1.address ()), result::accepted);

	// One inbound connection fills the inbound cap and leaves the outbound one alone
	index.insert (connection_type::inbound, peer1, nullptr, nullptr);
	ASSERT_EQ (index.size (), 1);
	ASSERT_EQ (index.size (connection_type::inbound), 1);
	ASSERT_EQ (index.size (connection_type::outbound), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer2.address ()), result::max_inbound);
	ASSERT_EQ (index.check (connection_type::outbound, peer2.address ()), result::accepted);

	// One outbound connection fills the other cap, both are listed in insertion order
	index.insert (connection_type::outbound, peer2, nullptr, nullptr);
	ASSERT_EQ (index.size (), 2);
	ASSERT_EQ (index.check (connection_type::outbound, peer1.address ()), result::max_outbound);
	auto const listed = index.list_connections ();
	ASSERT_EQ (listed.size (), 2);
	ASSERT_EQ (listed.front ().endpoint, peer1);
	ASSERT_EQ (listed.back ().endpoint, peer2);

	// Erasing the inbound connection hands it back and frees its cap only
	auto const erased = index.erase_if (to (peer1), origin);
	ASSERT_EQ (erased.size (), 1);
	ASSERT_EQ (erased.front ().type, connection_type::inbound);
	ASSERT_EQ (index.size (connection_type::inbound), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer1.address ()), result::accepted);
	ASSERT_EQ (index.check (connection_type::outbound, peer1.address ()), result::max_outbound);

	// An endpoint without a recorded connection yields nothing to erase
	ASSERT_TRUE (index.erase_if (to (peer1), origin).empty ());
}

/*
 * The per IP cap counts every open connection of an address whatever its port or direction, treats an IPv6 /48 as one address, and does not apply while the limit is left empty
 */
TEST (tcp_listener_index, per_ip)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip = 2;
	limits.max_per_subnetwork.reset ();
	limits.reconnect_cooldown = 0s;
	tcp_listener_index index{ limits };
	auto const peer1a = endpoint ("::ffff:10.0.0.1", 7075);
	auto const peer1b = endpoint ("::ffff:10.0.0.1", 7076);
	auto const peer2 = endpoint ("::ffff:10.0.0.2");

	// Two connections of one address, in either direction, reach its cap
	index.insert (connection_type::inbound, peer1a, nullptr, nullptr);
	ASSERT_EQ (index.count_per_ip (peer1a.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer1a.address ()), result::accepted);
	index.insert (connection_type::outbound, peer1b, nullptr, nullptr);
	ASSERT_EQ (index.count_per_ip (peer1a.address ()), 2);
	ASSERT_EQ (index.check (connection_type::inbound, peer1a.address ()), result::max_per_ip);
	ASSERT_EQ (index.check (connection_type::outbound, peer1a.address ()), result::max_per_ip);

	// Another address is unaffected
	ASSERT_EQ (index.count_per_ip (peer2.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer2.address ()), result::accepted);

	// Erasing one connection of the address makes room for one more
	ASSERT_EQ (index.erase_if (to (peer1a), origin).size (), 1);
	ASSERT_EQ (index.count_per_ip (peer1a.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer1a.address ()), result::accepted);

	// Two hosts of one IPv6 /48 share the cap, a host of another /48 does not
	auto const host1 = endpoint ("2001:db8:1::1");
	auto const host2 = endpoint ("2001:db8:1::2");
	auto const host3 = endpoint ("2001:db8:2::1");
	index.insert (connection_type::inbound, host1, nullptr, nullptr);
	index.insert (connection_type::inbound, host2, nullptr, nullptr);
	ASSERT_EQ (index.count_per_ip (host1.address ()), 2);
	ASSERT_EQ (index.check (connection_type::inbound, host1.address ()), result::max_per_ip);
	ASSERT_EQ (index.count_per_ip (host3.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, host3.address ()), result::accepted);

	// With the limit left empty nothing is refused for the address
	limits.max_per_ip.reset ();
	tcp_listener_index unlimited{ limits };
	unlimited.insert (connection_type::inbound, peer1a, nullptr, nullptr);
	unlimited.insert (connection_type::inbound, peer1b, nullptr, nullptr);
	unlimited.insert (connection_type::outbound, peer1a, nullptr, nullptr);
	ASSERT_EQ (unlimited.count_per_ip (peer1a.address ()), 3);
	ASSERT_EQ (unlimited.check (connection_type::inbound, peer1a.address ()), result::accepted);
}

/*
 * The per subnetwork cap spans distinct addresses of one IPv6 /32, leaves IPv4 addresses alone however many share a /24, and does not apply while the limit is left empty
 */
TEST (tcp_listener_index, per_subnetwork)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip.reset ();
	limits.max_per_subnetwork = 1;
	limits.reconnect_cooldown = 0s;
	tcp_listener_index index{ limits };
	auto const host1 = endpoint ("2001:db8:1::1");
	auto const host2 = endpoint ("2001:db8:2::1");
	auto const host3 = endpoint ("2001:db9::1");

	// One connection from the /32 refuses a different address of the same /32 and not one of another /32
	index.insert (connection_type::inbound, host1, nullptr, nullptr);
	ASSERT_EQ (index.count_per_subnetwork (host2.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, host2.address ()), result::max_per_subnetwork);
	ASSERT_EQ (index.count_per_subnetwork (host3.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, host3.address ()), result::accepted);

	// Erasing the connection frees the subnetwork
	ASSERT_EQ (index.erase_if (to (host1), origin).size (), 1);
	ASSERT_EQ (index.check (connection_type::inbound, host2.address ()), result::accepted);

	// IPv4 addresses are counted per /24 but never refused for it
	auto const peer1 = endpoint ("::ffff:10.0.0.1");
	auto const peer2 = endpoint ("::ffff:10.0.0.2");
	index.insert (connection_type::inbound, peer1, nullptr, nullptr);
	ASSERT_EQ (index.count_per_subnetwork (peer2.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer2.address ()), result::accepted);

	// With the limit left empty nothing is refused for the subnetwork
	limits.max_per_subnetwork.reset ();
	tcp_listener_index unlimited{ limits };
	unlimited.insert (connection_type::inbound, host1, nullptr, nullptr);
	unlimited.insert (connection_type::inbound, host2, nullptr, nullptr);
	ASSERT_EQ (unlimited.count_per_subnetwork (host1.address ()), 2);
	ASSERT_EQ (unlimited.check (connection_type::inbound, host1.address ()), result::accepted);
}

/*
 * A closed connection keeps its per IP slot for the cooldown: the slot is refused through the whole window, up to the moment before expiry, and free from the expiry on
 */
TEST (tcp_listener_index, cooldown)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip = 1;
	limits.max_per_subnetwork.reset ();
	limits.reconnect_cooldown = 10s;
	tcp_listener_index index{ limits };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// Erasing the connection leaves a cooldown in its place that still fills the slot
	index.insert (connection_type::inbound, peer, nullptr, nullptr);
	ASSERT_EQ (index.erase_if (to (peer), origin).size (), 1);
	ASSERT_EQ (index.size (), 0);
	ASSERT_EQ (index.cooldown_count (), 1);
	ASSERT_EQ (index.count_per_ip (peer.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer.address ()), result::max_per_ip);
	ASSERT_EQ (index.check (connection_type::outbound, peer.address ()), result::max_per_ip);

	// Just before the cooldown ends the slot is still taken
	index.update (origin + 9s);
	ASSERT_EQ (index.cooldown_count (), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer.address ()), result::max_per_ip);

	// From the expiry on the slot is free again
	index.update (origin + 10s);
	ASSERT_EQ (index.cooldown_count (), 0);
	ASSERT_EQ (index.count_per_ip (peer.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer.address ()), result::accepted);
}

/*
 * A cooldown holds the subnetwork slot as well, refusing another address of the same IPv6 /32 until it expires
 */
TEST (tcp_listener_index, cooldown_subnetwork)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip.reset ();
	limits.max_per_subnetwork = 1;
	limits.reconnect_cooldown = 10s;
	tcp_listener_index index{ limits };
	auto const host1 = endpoint ("2001:db8:1::1");
	auto const host2 = endpoint ("2001:db8:2::1");

	// The cooldown of one address counts against the whole /32
	index.insert (connection_type::inbound, host1, nullptr, nullptr);
	ASSERT_EQ (index.erase_if (to (host1), origin).size (), 1);
	ASSERT_EQ (index.count_per_subnetwork (host2.address ()), 1);
	ASSERT_EQ (index.check (connection_type::inbound, host2.address ()), result::max_per_subnetwork);

	// Once expired the /32 has room again
	index.update (origin + 10s);
	ASSERT_EQ (index.count_per_subnetwork (host2.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, host2.address ()), result::accepted);
}

/*
 * With the cooldown at zero a closed connection frees its slot at once and leaves nothing behind
 */
TEST (tcp_listener_index, cooldown_disabled)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip = 1;
	limits.reconnect_cooldown = 0s;
	tcp_listener_index index{ limits };
	auto const peer = endpoint ("::ffff:10.0.0.1");

	// Erasing the connection records no cooldown
	index.insert (connection_type::inbound, peer, nullptr, nullptr);
	ASSERT_EQ (index.check (connection_type::inbound, peer.address ()), result::max_per_ip);
	ASSERT_EQ (index.erase_if (to (peer), origin).size (), 1);
	ASSERT_EQ (index.cooldown_count (), 0);
	ASSERT_EQ (index.count_per_ip (peer.address ()), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer.address ()), result::accepted);
}

/*
 * Outbound connects in flight are capped per IP and in total: a second attempt to an address is refused while the first is listed, the total cap refuses any further address, and both free up as attempts are erased without ever counting as connections
 */
TEST (tcp_listener_index, attempts)
{
	tcp_listener_index::limits limits;
	limits.max_attempts = 2;
	limits.max_attempts_per_ip = 1;
	tcp_listener_index index{ limits };
	auto const peer1 = endpoint ("::ffff:10.0.0.1");
	auto const peer2 = endpoint ("::ffff:10.0.0.2");
	auto const peer3 = endpoint ("::ffff:10.0.0.3");

	// One attempt to an address uses up its per IP allowance
	ASSERT_EQ (index.check_attempt (peer1.address ()), result::accepted);
	index.insert_attempt (peer1, nullptr, origin);
	ASSERT_EQ (index.attempt_count (), 1);
	ASSERT_EQ (index.count_attempts (peer1.address ()), 1);
	ASSERT_EQ (index.check_attempt (peer1.address ()), result::max_attempts_per_ip);
	ASSERT_EQ (index.check_attempt (peer2.address ()), result::accepted);

	// A second address fills the total cap for any third, both are listed with their start
	index.insert_attempt (peer2, nullptr, origin + 1s);
	ASSERT_EQ (index.attempt_count (), 2);
	ASSERT_EQ (index.check_attempt (peer3.address ()), result::max_attempts);
	auto const listed = index.list_attempts ();
	ASSERT_EQ (listed.size (), 2);
	ASSERT_EQ (listed.front ().endpoint, peer1);
	ASSERT_EQ (listed.back ().start, origin + 1s);

	// Erasing an attempt hands it back and frees both its address and the total
	auto const erased = index.erase_attempts_if (to (peer1));
	ASSERT_EQ (erased.size (), 1);
	ASSERT_EQ (erased.front ().endpoint, peer1);
	ASSERT_EQ (index.count_attempts (peer1.address ()), 0);
	ASSERT_EQ (index.check_attempt (peer1.address ()), result::accepted);
	ASSERT_EQ (index.check_attempt (peer3.address ()), result::accepted);

	// An endpoint without a recorded attempt yields nothing to erase
	ASSERT_TRUE (index.erase_attempts_if (to (peer1)).empty ());

	// Attempts are not connections
	ASSERT_EQ (index.size (), 0);
	ASSERT_EQ (index.check (connection_type::outbound, peer2.address ()), result::accepted);
}

/*
 * Clearing the index hands back the open connections and the attempts in flight, forgets the cooldowns with them, and leaves every limit its full room again
 */
TEST (tcp_listener_index, clear)
{
	tcp_listener_index::limits limits;
	limits.max_per_ip = 1;
	limits.max_attempts_per_ip = 1;
	limits.reconnect_cooldown = 10s;
	tcp_listener_index index{ limits };
	auto const peer1 = endpoint ("::ffff:10.0.0.1");
	auto const peer2 = endpoint ("::ffff:10.0.0.2");

	// One entry of each kind, each of them filling a limit
	index.insert (connection_type::inbound, peer1, nullptr, nullptr);
	index.insert_attempt (peer2, nullptr, origin);
	index.insert (connection_type::inbound, peer2, nullptr, nullptr);
	ASSERT_EQ (index.erase_if (to (peer2), origin).size (), 1);
	ASSERT_EQ (index.check (connection_type::inbound, peer1.address ()), result::max_per_ip);
	ASSERT_EQ (index.check (connection_type::inbound, peer2.address ()), result::max_per_ip);
	ASSERT_EQ (index.check_attempt (peer2.address ()), result::max_attempts_per_ip);

	// The connection and the attempt come back, the cooldown is simply gone
	auto const contents = index.clear ();
	ASSERT_EQ (contents.connections.size (), 1);
	ASSERT_EQ (contents.connections.front ().endpoint, peer1);
	ASSERT_EQ (contents.attempts.size (), 1);
	ASSERT_EQ (contents.attempts.front ().endpoint, peer2);
	ASSERT_EQ (index.size (), 0);
	ASSERT_EQ (index.attempt_count (), 0);
	ASSERT_EQ (index.cooldown_count (), 0);
	ASSERT_EQ (index.check (connection_type::inbound, peer1.address ()), result::accepted);
	ASSERT_EQ (index.check (connection_type::inbound, peer2.address ()), result::accepted);
	ASSERT_EQ (index.check_attempt (peer2.address ()), result::accepted);
}
