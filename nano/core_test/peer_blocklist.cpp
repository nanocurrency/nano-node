#include <nano/lib/container_info.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * Node id entries are matched exactly and can be added and removed once each
 */
TEST (peer_blocklist, node_id)
{
	nano::peer_blocklist blocklist;
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_TRUE (blocklist.empty ());
	ASSERT_FALSE (blocklist.contains (key1.pub));

	ASSERT_TRUE (blocklist.add (key1.pub));
	ASSERT_FALSE (blocklist.add (key1.pub)); // Already present
	ASSERT_TRUE (blocklist.contains (key1.pub));
	ASSERT_FALSE (blocklist.contains (key2.pub));
	ASSERT_EQ (1, blocklist.size ());

	ASSERT_TRUE (blocklist.remove (key1.pub));
	ASSERT_FALSE (blocklist.remove (key1.pub)); // Already removed
	ASSERT_FALSE (blocklist.contains (key1.pub));
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * An IPv4 address is one entry whether it is spelled plain or in the v4 mapped IPv6 form peers connect with
 */
TEST (peer_blocklist, address_v4_mapped)
{
	nano::peer_blocklist blocklist;
	auto const plain = boost::asio::ip::make_address ("192.0.2.1");
	auto const mapped = boost::asio::ip::make_address ("::ffff:192.0.2.1");
	auto const other = boost::asio::ip::make_address ("192.0.2.2");

	ASSERT_TRUE (blocklist.add (plain));
	ASSERT_FALSE (blocklist.add (mapped)); // Same entry
	ASSERT_EQ (1, blocklist.size ());
	ASSERT_TRUE (blocklist.contains (plain));
	ASSERT_TRUE (blocklist.contains (mapped));
	ASSERT_FALSE (blocklist.contains (other));

	ASSERT_TRUE (blocklist.remove (mapped));
	ASSERT_FALSE (blocklist.contains (plain));
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * IPv6 entries block a single address, not its neighbours in the same subnet
 */
TEST (peer_blocklist, address_v6)
{
	nano::peer_blocklist blocklist;
	auto const address = boost::asio::ip::make_address ("2001:db8::1");
	auto const neighbour = boost::asio::ip::make_address ("2001:db8::2");

	ASSERT_TRUE (blocklist.add (address));
	ASSERT_TRUE (blocklist.contains (address));
	ASSERT_FALSE (blocklist.contains (neighbour));

	ASSERT_TRUE (blocklist.remove (address));
	ASSERT_FALSE (blocklist.remove (neighbour)); // Never present
	ASSERT_TRUE (blocklist.empty ());
}

/*
 * Node ids and addresses are independent entries that are counted together and reported apart
 */
TEST (peer_blocklist, mixed)
{
	nano::peer_blocklist blocklist;
	nano::keypair key;
	auto const address = boost::asio::ip::make_address ("192.0.2.1");

	ASSERT_TRUE (blocklist.add (key.pub));
	ASSERT_TRUE (blocklist.add (address));
	ASSERT_EQ (2, blocklist.size ());

	ASSERT_TRUE (blocklist.remove (key.pub));
	ASSERT_EQ (1, blocklist.size ());
	ASSERT_TRUE (blocklist.contains (address));
	ASSERT_FALSE (blocklist.contains (key.pub));

	auto const info = blocklist.container_info ();
	ASSERT_EQ (2, info.entries ().size ());
	auto entry = info.entries ().begin ();
	ASSERT_EQ ("node_ids", entry->name);
	ASSERT_EQ (0, entry->size);
	++entry;
	ASSERT_EQ ("addresses", entry->name);
	ASSERT_EQ (1, entry->size);
}

/*
 * A connection from a blocklisted address is refused before the handshake, and no channel appears on either side.
 * Removing the entry lets the same peer connect.
 */
TEST (peer_blocklist, address_inbound)
{
	nano::test::system system (1);
	auto node0 = system.nodes[0];
	auto node1 = nano::test::add_outer_node (system);
	auto const loopback = boost::asio::ip::address_v6::loopback ();

	ASSERT_TRUE (node0->peer_blocklist.add (loopback));
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::blocklisted, nano::stat::dir::in) >= 1);
	ASSERT_TIMELY (5s, node1->tcp_listener.connection_count (nano::transport::tcp_listener::connection_type::outbound) == 0);
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node1->network.size ());
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_channels, nano::stat::detail::channel_accepted));

	ASSERT_TRUE (node0->peer_blocklist.remove (loopback));

	// Clear the failed attempt, as a live node does periodically
	node1->network.cleanup (std::chrono::steady_clock::now ());
	node1->network.syn_cookies.purge (std::chrono::steady_clock::now ());

	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->network.find_node_id (node1->get_node_id ()) != nullptr);
	ASSERT_TIMELY (5s, node1->network.find_node_id (node0->get_node_id ()) != nullptr);
}

/*
 * No connection is initiated towards a blocklisted address, neither by peer merging nor by a direct connect
 */
TEST (peer_blocklist, address_outbound)
{
	nano::test::system system (1);
	auto node0 = system.nodes[0];
	auto node1 = nano::test::add_outer_node (system);
	auto const loopback = boost::asio::ip::address_v6::loopback ();

	ASSERT_TRUE (node0->peer_blocklist.add (loopback));

	ASSERT_FALSE (node0->network.track_reachout (node1->network.endpoint ()));
	ASSERT_FALSE (node0->network.merge_peer (node1->network.endpoint ()));
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::network, nano::stat::detail::merge_peer));

	ASSERT_FALSE (node0->tcp_listener.connect (loopback, node1->network.endpoint ().port ()));
	ASSERT_EQ (1, node0->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::blocklisted, nano::stat::dir::out));
	ASSERT_EQ (0, node0->tcp_listener.attempt_count ());

	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node1->network.size ());
}

/*
 * A peer whose node id is blocklisted is refused once its handshake proves the id, so the blocking node never gets a channel to it.
 * The peer briefly holds a channel of its own until the closed socket is noticed.
 * Other peers from the same address still connect.
 */
TEST (peer_blocklist, node_id_inbound)
{
	nano::test::system system (1);
	auto node0 = system.nodes[0];
	auto node1 = nano::test::add_outer_node (system);

	ASSERT_TRUE (node0->peer_blocklist.add (node1->get_node_id ()));
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_channels, nano::stat::detail::channel_accepted));
	ASSERT_TIMELY (5s, node1->network.find_node_id (node0->get_node_id ()) == nullptr);

	// A node that is not blocklisted connects from the same address
	auto node2 = system.add_node ();
	ASSERT_NE (nullptr, node0->network.find_node_id (node2->get_node_id ()));
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));
}

/*
 * The blocking node refuses a blocklisted node id it connected to itself, once the handshake reveals it
 */
TEST (peer_blocklist, node_id_outbound)
{
	nano::test::system system (1);
	auto node0 = system.nodes[0];
	auto node1 = nano::test::add_outer_node (system);

	ASSERT_TRUE (node0->peer_blocklist.add (node1->get_node_id ()));
	ASSERT_TRUE (node0->network.merge_peer (node1->network.endpoint ()));
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));
	ASSERT_EQ (0, node0->network.size ());
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_channels, nano::stat::detail::channel_accepted));
	ASSERT_TIMELY (5s, node1->network.find_node_id (node0->get_node_id ()) == nullptr);
}

/*
 * A peer that is blocklisted by node id while connected is dropped by the next connection cleanup and refused when it comes back
 */
TEST (peer_blocklist, disconnect_node_id)
{
	auto const test_start = std::chrono::steady_clock::now ();

	nano::test::system system (2);
	auto node0 = system.nodes[0];
	auto node1 = system.nodes[1];
	ASSERT_NE (nullptr, node0->network.find_node_id (node1->get_node_id ()));

	ASSERT_TRUE (node0->peer_blocklist.add (node1->get_node_id ()));

	// Nothing has been idle since the test started, so only the blocklisted channel closes
	node0->network.cleanup (test_start);
	ASSERT_EQ (1, node0->stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::blocklisted));
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::idle));
	ASSERT_TIMELY (5s, node0->network.find_node_id (node1->get_node_id ()) == nullptr);
	ASSERT_TIMELY (5s, node1->network.find_node_id (node0->get_node_id ()) == nullptr);

	// The dropped peer is refused when it reconnects
	node1->network.cleanup (std::chrono::steady_clock::now ());
	node1->network.syn_cookies.purge (std::chrono::steady_clock::now ());
	ASSERT_TRUE (node1->network.merge_peer (node0->network.endpoint ()));
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));
	ASSERT_EQ (0, node0->network.size ());
}

/*
 * A peer that is blocklisted by address while connected is dropped by the next connection cleanup
 */
TEST (peer_blocklist, disconnect_address)
{
	auto const test_start = std::chrono::steady_clock::now ();

	nano::test::system system (2);
	auto node0 = system.nodes[0];
	auto node1 = system.nodes[1];
	ASSERT_EQ (1, node0->network.size ());

	ASSERT_TRUE (node0->peer_blocklist.add (boost::asio::ip::address_v6::loopback ()));

	node0->network.cleanup (test_start);
	ASSERT_EQ (1, node0->stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::blocklisted));
	ASSERT_TIMELY_EQ (5s, node0->network.size (), 0);
	ASSERT_TIMELY_EQ (5s, node1->network.size (), 0);
}

/*
 * A blocklisted channel that is also idle is counted as dropped by the blocklist, not as idle
 */
TEST (peer_blocklist, disconnect_idle)
{
	nano::test::system system (2);
	auto node0 = system.nodes[0];
	auto node1 = system.nodes[1];
	ASSERT_EQ (1, node0->network.size ());

	ASSERT_TRUE (node0->peer_blocklist.add (node1->get_node_id ()));

	// A cutoff in the future makes every channel idle
	node0->network.cleanup (std::chrono::steady_clock::now () + 1h);
	ASSERT_EQ (1, node0->stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::blocklisted));
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::idle));
	ASSERT_TIMELY_EQ (5s, node0->network.size (), 0);
}
