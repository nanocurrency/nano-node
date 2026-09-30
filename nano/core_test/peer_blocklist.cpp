#include <nano/lib/config.hpp>
#include <nano/lib/container_info.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/peer_blocklist.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <fstream>

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
 * Node ids and IP addresses are independent entries that are counted together and reported apart
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
	ASSERT_EQ ("ip_addresses", entry->name);
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

/*
 * The config serializes to the file format and reads back with the same entries
 */
TEST (peer_blocklist, config_round_trip)
{
	nano::peer_blocklist_config written;
	written.node_ids.push_back (nano::keypair{}.pub);
	written.node_ids.push_back (nano::keypair{}.pub);
	written.ip_addresses.push_back (boost::asio::ip::make_address ("192.0.2.1"));
	written.ip_addresses.push_back (boost::asio::ip::make_address ("2001:db8::1"));

	nano::tomlconfig toml;
	ASSERT_FALSE (written.serialize_toml (toml));

	nano::peer_blocklist_config read;
	ASSERT_FALSE (read.deserialize_toml (toml));
	ASSERT_EQ (read.node_ids, written.node_ids);
	ASSERT_EQ (read.ip_addresses, written.ip_addresses);
}

/*
 * A file with both lists is read from the data directory, a file with one list leaves the other empty, and a missing file leaves the config empty without creating it
 */
TEST (peer_blocklist, config_file)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);

	nano::peer_blocklist_config missing;
	ASSERT_FALSE (nano::read_peer_blocklist_config (missing, path));
	ASSERT_TRUE (missing.node_ids.empty ());
	ASSERT_TRUE (missing.ip_addresses.empty ());
	ASSERT_FALSE (std::filesystem::exists (path / nano::peer_blocklist_filename));

	nano::keypair key;
	{
		std::ofstream file{ path / nano::peer_blocklist_filename };
		file << "# Peers refused by this node\n"
			 << "[blocklist]\n"
			 << "node_ids = [\"" << key.pub.to_node_id () << "\"]\n"
			 << "ip_addresses = [\"192.0.2.1\", \"::ffff:192.0.2.2\", \"2001:db8::1\"]\n";
	}
	nano::peer_blocklist_config config;
	auto error = nano::read_peer_blocklist_config (config, path);
	ASSERT_FALSE (error) << error.get_message ();
	ASSERT_EQ (config.node_ids, std::deque<nano::account>{ key.pub });
	std::deque<boost::asio::ip::address> const expected{ boost::asio::ip::make_address ("192.0.2.1"), boost::asio::ip::make_address ("::ffff:192.0.2.2"), boost::asio::ip::make_address ("2001:db8::1") };
	ASSERT_EQ (config.ip_addresses, expected);

	{
		std::ofstream file{ path / nano::peer_blocklist_filename };
		file << "[blocklist]\nnode_ids = [\"" << key.pub.to_node_id () << "\"]\n";
	}
	nano::peer_blocklist_config node_ids_only;
	error = nano::read_peer_blocklist_config (node_ids_only, path);
	ASSERT_FALSE (error) << error.get_message ();
	ASSERT_EQ (node_ids_only.node_ids, std::deque<nano::account>{ key.pub });
	ASSERT_TRUE (node_ids_only.ip_addresses.empty ());
}

/*
 * An entry that is not a node id is reported with the file name and the entry, an account address included
 */
TEST (peer_blocklist, config_invalid_node_id)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	auto const file = path / nano::peer_blocklist_filename;

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nnode_ids = [\"node_invalid\"]\n";
	}
	nano::peer_blocklist_config garbled;
	auto error = nano::read_peer_blocklist_config (garbled, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: Invalid node id: node_invalid");

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nnode_ids = [\"" << nano::keypair{}.pub.to_account () << "\"]\n";
	}
	nano::peer_blocklist_config account;
	error = nano::read_peer_blocklist_config (account, path);
	ASSERT_EQ (error.get_message ().find ("peer-blocklist.toml: Invalid node id: nano_"), 0) << error.get_message ();
}

/*
 * An entry that is not an IP address is reported with the file name and the entry, host names included
 */
TEST (peer_blocklist, config_invalid_address)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	auto const file = path / nano::peer_blocklist_filename;

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nip_addresses = [\"192.0.2.1\", \"192.0.2.256\"]\n";
	}
	nano::peer_blocklist_config out_of_range;
	auto error = nano::read_peer_blocklist_config (out_of_range, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: Invalid IP address: 192.0.2.256");

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nip_addresses = [\"peer.example.com\"]\n";
	}
	nano::peer_blocklist_config host_name;
	error = nano::read_peer_blocklist_config (host_name, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: Invalid IP address: peer.example.com");
}

/*
 * A list that is not an array of strings is an error rather than an empty list, so a mistyped file cannot silently block nothing
 */
TEST (peer_blocklist, config_wrong_type)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);
	auto const file = path / nano::peer_blocklist_filename;

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nnode_ids = \"" << nano::keypair{}.pub.to_node_id () << "\"\n";
	}
	nano::peer_blocklist_config single_value;
	auto error = nano::read_peer_blocklist_config (single_value, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: node_ids must be an array of strings");

	{
		std::ofstream stream{ file };
		stream << "[blocklist]\nip_addresses = [[\"192.0.2.1\"]]\n";
	}
	nano::peer_blocklist_config nested;
	error = nano::read_peer_blocklist_config (nested, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: ip_addresses must be an array of strings");
}

/*
 * A blocklist that is not a table is an error, never a silently ignored list
 */
TEST (peer_blocklist, config_not_a_table)
{
	auto path = nano::unique_path ();
	std::filesystem::create_directories (path);

	{
		std::ofstream stream{ path / nano::peer_blocklist_filename };
		stream << "blocklist = 1\n";
	}
	nano::peer_blocklist_config config;
	auto error = nano::read_peer_blocklist_config (config, path);
	ASSERT_EQ (error.get_message (), "peer-blocklist.toml: Configuration node is not a table: blocklist");
}

/*
 * A node built from a config with entries refuses them from the start, duplicates counted once
 */
TEST (peer_blocklist, config_applied)
{
	nano::test::system system;
	nano::keypair key;
	auto const address = boost::asio::ip::make_address ("192.0.2.1");

	auto config = system.default_config ();
	config.peer_blocklist->node_ids = { key.pub, key.pub };
	config.peer_blocklist->ip_addresses = { address, boost::asio::ip::make_address ("::ffff:192.0.2.1") };
	auto node = system.add_node (config);

	ASSERT_TRUE (node->peer_blocklist.contains (key.pub));
	ASSERT_TRUE (node->peer_blocklist.contains (address));
	ASSERT_EQ (2, node->peer_blocklist.size ());
}
