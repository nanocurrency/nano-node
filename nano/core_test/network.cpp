#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/duplicate_filter.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/transport/tcp_channel.hpp>
#include <nano/node/transport/tcp_config.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/peer.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>
#include <nano/test_common/topology.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

/*
 * A configured peering port is the port the network and the listener report
 */
TEST (network, construction_with_specified_port)
{
	nano::test::system system;
	auto const port = nano::test::pick_free_port ();
	ASSERT_NE (0, port);
	nano::node_config node_config = system.default_config ();
	node_config.peering_port = port;
	auto & node = *system.add_node (node_config);

	ASSERT_EQ (port, node.network.port);
	ASSERT_EQ (port, node.network.endpoint ().port ());
	ASSERT_EQ (port, node.tcp_listener.endpoint ().port ());
}

/*
 * A node given no particular port still binds one, which the network and the listener agree on
 */
TEST (network, construction_without_specified_port)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto const port = node.network.port.load ();
	ASSERT_NE (0, port);
	ASSERT_EQ (port, node.network.endpoint ().port ());
	ASSERT_EQ (port, node.tcp_listener.endpoint ().port ());
}

/*
 * A channel's last packet received time moves forward when the peer sends on it
 */
TEST (network, last_contacted)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);

	// The time of the last packet so far, and a moment for the clock to move past it
	auto const before = channel->get_last_packet_received ();
	ASSERT_TIMELY (3s, std::chrono::steady_clock::now () > before);

	// A keepalive from the peer moves the time forward
	ASSERT_TRUE (peer.send (nano::messages::keepalive{ nano::dev::network_params.network }));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	ASSERT_GT (channel->get_last_packet_received (), before);
}

/*
 * A flooded block reaches a fanout of the peers and no more, and the count returned is the number of channels that queued it
 */
TEST (network, flood_block)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();

	// Publishes that arrived at the three peers
	auto received = [&] () {
		size_t count = 0;
		for (auto const & other : system.nodes)
		{
			if (other.get () != &node)
			{
				count += other->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in);
			}
		}
		return count;
	};

	// The fanout of three peers is two, so two of them get the block and the third never does
	ASSERT_EQ (2, node.network.fanout ());
	ASSERT_EQ (2, node.network.flood_block (block, nano::transport::traffic_type::test));
	ASSERT_TIMELY_EQ (5s, 2, received ());
	ASSERT_ALWAYS_EQ (100ms, 2, received ());
}

/*
 * A block processed as active goes into the node's ledger and out to its peers as a publish
 */
TEST (network, send_valid_publish)
{
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const latest = node2.latest (nano::dev::genesis_key.pub);
	nano::keypair key;
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (latest)
				 .destination (key.pub)
				 .balance (50)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();

	// node2 takes the block and publishes it, node1 receives the publish
	node2.process_active (block);
	ASSERT_TIMELY (10s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) >= 1);
	ASSERT_TIMELY_EQ (10s, block->hash (), node2.latest (nano::dev::genesis_key.pub));
	ASSERT_EQ (50, node2.balance (nano::dev::genesis_key.pub));
}

/*
 * Once the channels from one IP reach the per IP cap, a reachout to another port of that IP is refused
 */
TEST (network, peer_max_tcp_attempts)
{
	nano::test::system system;
	// Cleanup off, so the channels counted against the cap stay as they are
	nano::node_flags node_flags;
	node_flags.disable_connection_cleanup = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 3;
	auto & node = *system.add_node (node_config, node_flags);

	// Three peers on the loopback IP, none reaching out itself so that every channel is one this node made
	for (auto i = 0; i < node_config.network->max_peers_per_ip; ++i)
	{
		nano::node_config peer_config = system.default_config ();
		peer_config.network->peer_reachout = {};
		peer_config.network->cached_peer_reachout = {};
		auto const peer = system.make_disconnected_node (peer_config, node_flags);
		ASSERT_TRUE (node.network.merge_peer (peer->network.endpoint ()));
	}
	ASSERT_TIMELY_EQ (15s, node_config.network->max_peers_per_ip, node.network.size ());

	// A fourth endpoint on the same IP is refused and counted
	ASSERT_FALSE (node.network.tcp_channels.track_reachout (nano::endpoint (node.network.endpoint ().address (), system.get_available_port ())));
	ASSERT_LE (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_ip, nano::stat::dir::out));
}

/*
 * Reachout attempts count toward the per subnetwork cap like channels do, so a fourth address of the subnetwork is refused before any connection exists
 */
TEST (network, peer_max_tcp_attempts_subnetwork)
{
	nano::test::system system;
	// The per IP cap is off, so only the subnetwork cap can refuse
	nano::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_subnetwork = 3;
	auto & node = *system.add_node (node_config, node_flags);

	// Attempts to 127.0.0.1, 127.0.0.2 and 127.0.0.3, all in one subnetwork, are tracked without a channel among them
	for (auto i = 0; i < node.config.network->max_peers_per_subnetwork; ++i)
	{
		auto const address = boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (0x7f000001 + i));
		ASSERT_TRUE (node.network.tcp_channels.track_reachout (nano::endpoint (address, system.get_available_port ())));
	}
	ASSERT_EQ (0, node.network.size ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_subnetwork, nano::stat::dir::out));

	// A fourth attempt in the subnetwork is refused on the attempts alone
	ASSERT_FALSE (node.network.tcp_channels.track_reachout (nano::endpoint (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"), system.get_available_port ())));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_subnetwork, nano::stat::dir::out));
}

namespace
{
// Skip the first 8 bytes of the message header, which is the common header for all messages
std::vector<uint8_t> message_payload_to_bytes (nano::messages::message const & message)
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		message.serialize (stream);
	}
	debug_assert (bytes.size () > nano::messages::message_header::size);
	return std::vector<uint8_t> (bytes.begin () + nano::messages::message_header::size, bytes.end ());
}

// Whether the node's container still lists the channel
bool has_channel (nano::node & node, std::shared_ptr<nano::transport::channel> const & channel)
{
	auto const channels = node.network.list ();
	return std::find (channels.begin (), channels.end (), channel) != channels.end ();
}
}

/*
 * The second of two identical publishes over TCP is caught by the duplicate filter
 */
TEST (network, duplicate_detection)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	nano::messages::publish const publish{ nano::dev::network_params.network, nano::dev::genesis };

	// The first copy passes the filter and is dispatched
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));

	// The second is counted as a duplicate and goes no further
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
}

/*
 * The second of two identical votes over TCP is caught by the duplicate filter
 */
TEST (network, duplicate_vote_detection)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	nano::messages::confirm_ack const message{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }) };

	// The first copy passes the filter and is dispatched
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));

	// The second is counted as a duplicate and goes no further
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
}

/*
 * A vote the processor cannot queue is cleared from the duplicate filter, so a retry of it is not treated as a duplicate
 */
TEST (network, duplicate_revert_vote)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.vote_processor->enable = false; // Do not drain queued votes
	node_config.vote_processor->max_non_pr_queue = 1;
	node_config.vote_processor->max_pr_queue = 1;
	auto & node = *system.add_node (node_config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// Two votes of one representative
	nano::messages::confirm_ack const message1{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }, /* timestamp */ 1) };
	auto const bytes1 = message_payload_to_bytes (message1);
	nano::messages::confirm_ack const message2{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }, /* timestamp */ 2) };
	auto const bytes2 = message_payload_to_bytes (message2);

	// The first vote fills the queue of one and stays in the filter
	ASSERT_TRUE (peer.send (message1));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_TRUE (node.network.filter.contains (node.network.filter.hash (message1.header, bytes1)));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in));

	// The second vote is dropped by the full queue, which clears it from the filter before counting the drop
	ASSERT_TRUE (peer.send (message2));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_FALSE (node.network.filter.contains (node.network.filter.hash (message2.header, bytes2)));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * A publish the block processor refuses is erased from the duplicate filter under the digest it carries, so the peer's next copy of it is received again.
 * One that carries no digest went through no filter, and its refusal leaves the filter alone
 */
TEST (network, duplicate_revert_publish)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.block_processor->max_peer_queue = 0; // The block processor takes no blocks from peers
	auto & node = *system.add_node (node_config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	nano::messages::publish const publish{ nano::dev::network_params.network, nano::dev::genesis };
	auto const digest = node.network.filter.hash (*nano::dev::genesis);

	// The peer's copy carries the digest it entered the filter under, and its refusal clears it from the filter before the drop is counted
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_FALSE (node.network.filter.contains (digest));

	// The peer's next copy gets as far as the first did, where a duplicate would have stopped at the filter
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_FALSE (node.network.filter.contains (digest));

	// A copy that comes in without a digest is refused as well, and leaves an entry of the same block in place
	ASSERT_TRUE (node.network.filter.insert (digest));
	ASSERT_EQ (0, publish.digest);
	node.inbound (publish, nano::test::fake_channel (node));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TRUE (node.network.filter.contains (digest));
}

/*
 * A fork that a full election turns away leaves the duplicate filter, so the peer's next copy of it is received again instead of being taken for a duplicate.
 * A fork the election holds stays filtered
 */
TEST (network, duplicate_revert_rejected_fork)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.backlog_scan->enable = false;
	auto & node = *system.add_node (config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// Eleven forks of one root, one more than an election holds
	nano::keypair destination;
	std::deque<nano::messages::publish> forks;
	for (int i = 0; i < 11; ++i)
	{
		auto const fork = nano::state_block_builder ()
						  .account (nano::dev::genesis_key.pub)
						  .previous (nano::dev::genesis->hash ())
						  .representative (nano::dev::genesis_key.pub)
						  .balance (nano::dev::constants.genesis_amount - 1 - i)
						  .link (destination.pub)
						  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
						  .work (*system.work.generate (nano::dev::genesis->hash ()))
						  .build ();
		forks.emplace_back (nano::dev::network_params.network, fork);
	}

	// The peer publishes the first ten, which fill one election to its block limit
	for (int i = 0; i < 10; ++i)
	{
		ASSERT_TRUE (peer.send (forks[i]));
	}
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, (election = node.active.election (forks[0].block->qualified_root ())) != nullptr);
	ASSERT_TIMELY_EQ (5s, 10, election->blocks ().size ());

	// The eleventh, which no vote backs, is received and turned away by the full election, which erases it from the filter
	auto const & rejected = forks[10];
	ASSERT_TRUE (peer.send (rejected));
	ASSERT_TIMELY_EQ (5s, 11, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY (5s, !node.network.filter.contains (node.network.filter.hash (*rejected.block)));
	ASSERT_FALSE (election->contains_block (rejected.block->hash ()));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));

	// The peer's next copy of it is received again
	ASSERT_TRUE (peer.send (rejected));
	ASSERT_TIMELY_EQ (5s, 12, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));

	// A second copy of a fork the election holds is caught as a duplicate
	ASSERT_TRUE (peer.send (forks[0]));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (12, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
}

/*
 * The network's cleanup loop advances the duplicate filter, so a message it caught gets through again once the configured cutoff has passed
 * Before the cutoff a resend is still caught as a duplicate, after it the resend is dispatched like a new message
 */
TEST (network, expire_duplicate_filter)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	std::chrono::seconds const cutoff{ 3 };
	node_config.network->duplicate_filter_cutoff = cutoff.count ();
	auto & node = *system.add_node (node_config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	nano::messages::confirm_ack const message{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }) };
	auto const bytes = message_payload_to_bytes (message);

	// The first copy passes the filter and is dispatched, the second is caught as a duplicate
	auto const first_sent = std::chrono::steady_clock::now ();
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));

	// A resend well within the cutoff is still caught
	std::this_thread::sleep_for (1s);
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));

	// The entry ages out, and not before the cutoff has passed since the first copy was sent
	ASSERT_TIMELY (10s, !node.network.filter.contains (node.network.filter.hash (message.header, bytes)));
	ASSERT_GE (std::chrono::steady_clock::now () - first_sent, cutoff);

	// A resend now passes the filter and is dispatched like a new message
	ASSERT_TRUE (peer.send (message));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * A peer struck off twice is refused when it connects and never reached out to, until it is removed from the exclusion list
 */
TEST (network, tcp_no_accept_excluded_peers)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer_listener listener{ node };
	auto const endpoint = nano::transport::map_endpoint_to_tcp (listener.endpoint ());

	// The second strike reaches the score limit and excludes the peer
	node.network.excluded_peers.add (endpoint);
	ASSERT_FALSE (node.network.excluded_peers.check (endpoint));
	node.network.excluded_peers.add (endpoint);
	ASSERT_TRUE (node.network.excluded_peers.check (endpoint));

	// A connection from its address is refused at the listener and no channel appears
	nano::test::peer refused{ node };
	ASSERT_TIMELY (5s, !refused.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::excluded));
	ASSERT_EQ (0, node.network.size ());

	// Nor is it reached out to
	ASSERT_FALSE (node.network.track_reachout (listener.endpoint ()));

	// Removing the exclusion lets a connection through
	node.network.excluded_peers.remove (endpoint);
	ASSERT_FALSE (node.network.excluded_peers.check (endpoint));
	ASSERT_TRUE (node.network.merge_peer (listener.endpoint ()));
	auto const peer = listener.accept ();
	ASSERT_NE (nullptr, peer);
	ASSERT_TRUE (peer->handshake ());
	ASSERT_EQ (1, node.network.size ());
}

/*
 * A cleanup closes the channels that have not sent since its cutoff and leaves the ones that have
 */
TEST (network, cleanup_purge)
{
	auto const start = std::chrono::steady_clock::now ();
	nano::test::system system;
	auto & node = *system.add_node ();

	// Without channels there is nothing to purge, whatever the cutoff
	ASSERT_EQ (0, node.network.size ());
	node.network.cleanup (start);
	node.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, node.network.size ());

	// A channel that has sent since the cutoff survives
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	ASSERT_EQ (1, node.network.size ());
	node.network.cleanup (start);
	ASSERT_EQ (1, node.network.size ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_channels_purge));

	// A cutoff later than any send the channel can have made marks it idle, so it is closed and counted
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_TRUE (channel->alive ());
	node.network.cleanup (std::chrono::steady_clock::now () + 1h);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::idle));
	ASSERT_TIMELY (5s, !channel->alive ());
	ASSERT_TIMELY (5s, !peer.alive ());
}

/*
 * A self keepalive leads with this node's listening port on the unspecified address and an empty slot, then the peers, then unspecified slots
 */
TEST (network, fill_keepalive_self)
{
	nano::test::system system{ 2 };
	auto & node = *system.nodes[0];
	auto fill = [&node] () {
		std::array<nano::endpoint, 8> target;
		node.network.fill_keepalive_self (target);
		return target;
	};
	nano::endpoint const unspecified{ boost::asio::ip::address_v6{}, 0 };

	// The one peer takes the third slot once its keepalive has told this node its listening port
	ASSERT_TIMELY_EQ (5s, fill ()[2].port (), system.nodes[1]->network.port);

	// The first two slots are this node's own listening port and a blank, the slots after the peer stay unspecified
	auto const target = fill ();
	ASSERT_EQ (nano::endpoint (boost::asio::ip::address_v6{}, node.network.port), target[0]);
	ASSERT_EQ (unspecified, target[1]);
	ASSERT_TRUE (std::all_of (target.begin () + 3, target.end (), [&unspecified] (auto const & slot) { return slot == unspecified; }));
}

/*
 * A channel whose own socket is closed reports itself dead and is dropped from the container
 */
TEST (network, purge_dead_channel)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// The one channel and the socket under it
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	auto const sockets = node.tcp_listener.all_sockets ();
	ASSERT_EQ (1, sockets.size ());
	auto const socket = sockets.front ();
	ASSERT_NE (nullptr, socket);

	// Closing the socket kills the channel, which is then dropped
	ASSERT_TRUE (channel->alive ());
	socket->close ();
	ASSERT_TIMELY (10s, !channel->alive ());
	ASSERT_TIMELY (5s, !has_channel (node, channel));
}

/*
 * A channel whose remote end closes the connection reports itself dead and is dropped from the container
 */
TEST (network, purge_dead_channel_remote)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_TRUE (channel->alive ());

	// The peer closing its end kills the node's channel, which the node then drops
	peer.close ();
	ASSERT_TIMELY (5s, !channel->alive ());
	ASSERT_TIMELY (5s, !has_channel (node, channel));
}

/*
 * A vote flood reaches a fanout of the channels, all of them, the non representatives or the principal representatives, and returns how many it reached
 */
TEST (network, flood_vote)
{
	nano::test::system system{ 4 };
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, 3, node.network.size ());

	// Genesis votes from the second node, which makes that peer the one principal representative
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (5s, 1, node.rep_crawler.representative_count ());
	ASSERT_EQ (2, node.network.fanout ());

	// A plain flood takes a fanout of the three channels, a flood to all takes all three
	auto const vote = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () });
	ASSERT_EQ (2, node.network.flood_vote (vote, nano::transport::traffic_type::test));
	ASSERT_EQ (3, node.network.flood_vote_all (vote, nano::transport::traffic_type::test));

	// At a scale wide enough for every channel the non representative flood still skips the representative, the representative flood takes only it
	ASSERT_EQ (2, node.network.flood_vote_non_pr (vote, 999.0f));
	ASSERT_EQ (1, node.network.flood_vote_pr (vote));
}

/*
 * Two nodes that know nothing of each other discover one another purely by connecting to a
 * peering-only node. Keeping no ledger does not stop it gossiping peers, which is what a
 * DNS-attached peering-only node exists to do.
 */
TEST (network, peer_discovery_via_peering_only_node)
{
	nano::test::system system;
	nano::node_flags peering_flags;
	peering_flags.peering_only = true;
	auto const node_c = system.make_disconnected_node (system.default_config (), peering_flags);
	auto const node_a = system.make_disconnected_node ();
	auto const node_b = system.make_disconnected_node ();
	ASSERT_EQ (nullptr, node_a->network.find_node_id (node_b->get_node_id ()));
	ASSERT_EQ (nullptr, node_b->network.find_node_id (node_a->get_node_id ()));

	// Both full nodes connect to the peering-only node and to nothing else
	node_a->network.merge_peer (node_c->network.endpoint ());
	node_b->network.merge_peer (node_c->network.endpoint ());
	ASSERT_TIMELY (10s, node_a->network.find_node_id (node_c->get_node_id ()));
	ASSERT_TIMELY (10s, node_b->network.find_node_id (node_c->get_node_id ()));

	// Its keepalives introduce them to each other
	ASSERT_TIMELY (30s, node_a->network.find_node_id (node_b->get_node_id ()));
	ASSERT_TIMELY (30s, node_b->network.find_node_id (node_a->get_node_id ()));

	// C brokered the introduction without ever holding a ledger
	ASSERT_EQ (1, node_c->ledger.block_count ());
}

/*
 * A sample holds at most the requested number of channels, each one once and each one passing the filter, and every channel when fewer than that pass.
 */
TEST (network, sample)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);

	// Two of the three channels, no channel twice
	auto const two = node.network.sample (2);
	ASSERT_EQ (2, two.size ());
	ASSERT_NE (two[0], two[1]);

	// Asking for more than there are gives every channel once
	auto const all = node.network.sample (10);
	ASSERT_EQ (3, all.size ());
	ASSERT_EQ (3, std::set (all.begin (), all.end ()).size ());

	// A channel the filter rejects never comes back, whatever the count
	auto const rejected = all[0];
	auto const filtered = node.network.sample (10, [&rejected] (auto const & channel) { return channel != rejected; });
	ASSERT_EQ (2, filtered.size ());
	ASSERT_EQ (0, std::ranges::count (filtered, rejected));
}

/*
 * Every channel is as likely as any other to come first in a sample, so a caller that keeps only the front of one still picks fairly.
 * A sample left in the order the channels are walked puts the oldest channel first in two thirds of these rounds and the second oldest never.
 */
TEST (network, sample_order_uniform)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);

	// Count which channel comes first in many samples of two
	std::map<std::shared_ptr<nano::transport::channel>, size_t> first;
	size_t const rounds = 3000;
	for (size_t round = 0; round < rounds; ++round)
	{
		auto const sample = node.network.sample (2);
		ASSERT_EQ (2, sample.size ());
		++first[sample.front ()];
	}

	// Each of the three comes first in about a third of the rounds, the bounds sit over seven standard deviations from a fair count
	ASSERT_EQ (3, first.size ());
	for (auto const & entry : first)
	{
		ASSERT_GT (entry.second, rounds / 3 * 8 / 10);
		ASSERT_LT (entry.second, rounds / 3 * 12 / 10);
	}
}

/*
 * A sample that leaves principal representatives out never returns their channels and picks fairly among the rest.
 * Keeping the first candidates that qualify from a sample left in walk order picks one of the two others twice as often as the other.
 */
TEST (network, sample_non_pr)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);

	// The genesis key holds all the weight, so the channel it is known by belongs to a principal representative
	std::shared_ptr<nano::transport::channel> const principal = node.network.find_node_id (system.nodes[1]->get_node_id ());
	ASSERT_NE (nullptr, principal);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, principal);
	ASSERT_TRUE (node.rep_crawler.is_pr (principal));

	// Asking for more than there are gives the two other channels
	auto const all = node.network.sample_non_pr (10);
	ASSERT_EQ (2, all.size ());
	ASSERT_EQ (0, std::ranges::count (all, principal));

	// Each of the two is picked in about half of many single picks, the bounds sit over ten standard deviations from a fair count
	std::map<std::shared_ptr<nano::transport::channel>, size_t> picks;
	size_t const rounds = 3000;
	for (size_t round = 0; round < rounds; ++round)
	{
		auto const sample = node.network.sample_non_pr (1);
		ASSERT_EQ (1, sample.size ());
		++picks[sample.front ()];
	}
	ASSERT_EQ (2, picks.size ());
	ASSERT_EQ (0, picks.count (principal));
	for (auto const & entry : picks)
	{
		ASSERT_GT (entry.second, rounds / 2 * 8 / 10);
		ASSERT_LT (entry.second, rounds / 2 * 12 / 10);
	}
}

/*
 * Excluding a live channel strikes its peer once, closes it and drops it from the container at once
 */
TEST (network, exclude)
{
	nano::test::system system (2);
	auto & node0 = *system.nodes[0];
	auto & node1 = *system.nodes[1];
	auto const channel = node0.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);
	auto const tcp_channel = std::dynamic_pointer_cast<nano::transport::tcp_channel> (channel);
	ASSERT_NE (nullptr, tcp_channel);
	auto const endpoint = nano::transport::map_endpoint_to_tcp (channel->get_remote_endpoint ());
	ASSERT_TRUE (channel->alive ());
	ASSERT_EQ (0, node0.network.excluded_peers.score (endpoint));

	node0.network.exclude (channel);

	// One strike, below the score limit, so the peer may still reconnect
	ASSERT_EQ (1, node0.network.excluded_peers.score (endpoint));
	ASSERT_FALSE (node0.network.excluded_peers.check (endpoint));

	// Gone from the container at once, not by a later purge
	auto const channels = node0.network.list ();
	ASSERT_EQ (channels.end (), std::find (channels.begin (), channels.end (), channel));

	// Closed, not merely dropped, and the other side sees the disconnect
	ASSERT_FALSE (channel->alive ());
	ASSERT_FALSE (tcp_channel->socket->alive ());
	ASSERT_TIMELY (5s, node1.stats.count (nano::stat::type::tcp_channels, nano::stat::detail::erase_dead) >= 1);
}

/*
 * The list holds every channel in connection order, cut to the count asked for, keeping only those passing the filter or on at least the version given
 */
TEST (network, list)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);

	// Every channel, in the order the peers connected
	auto const all = node.network.list ();
	ASSERT_EQ (3, all.size ());
	for (size_t i = 0; i < all.size (); ++i)
	{
		ASSERT_EQ (system.nodes[i + 1]->get_node_id (), all[i]->get_node_id ());
	}

	// A count cuts the list at the back, more than there are gives them all
	auto const two = node.network.list (2);
	ASSERT_EQ (2, two.size ());
	ASSERT_EQ (all[0], two[0]);
	ASSERT_EQ (all[1], two[1]);
	ASSERT_EQ (3, node.network.list (10).size ());

	// The filter drops a channel before the cut
	auto const not_first = [&all] (auto const & channel) { return channel != all[0]; };
	auto const filtered = node.network.list (0, not_first);
	ASSERT_EQ (2, filtered.size ());
	ASSERT_EQ (all[1], filtered[0]);
	ASSERT_EQ (all[2], filtered[1]);
	ASSERT_EQ (all[1], node.network.list (1, not_first).front ());

	// The version form keeps the channels on at least that protocol version, every peer here runs the current one
	auto const version = node.network_params.network.protocol_version;
	ASSERT_EQ (3, node.network.list (0, version).size ());
	ASSERT_EQ (2, node.network.list (2, version).size ());
	ASSERT_TRUE (node.network.list (0, static_cast<uint8_t> (version + 1)).empty ());
}

/*
 * A message serialized once goes to every channel of a range, counted under the type given with the buffer, and the count returned is the number of channels that queued it
 */
TEST (network, send_to_buffer)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();
	nano::messages::publish const message{ nano::dev::network_params.network, block };

	// One buffer reaches all three channels and is counted out as a publish
	ASSERT_EQ (3, node.network.send_to (message.to_shared_const_buffer (), message.type (), nano::transport::traffic_type::test, node.network.list ()));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
	for (auto i = 1; i < 4; ++i)
	{
		ASSERT_TIMELY_EQ (5s, 1, system.nodes[i]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	}

	// An empty range sends nothing
	ASSERT_EQ (0, node.network.send_to (message, nano::transport::traffic_type::test, nano::transport::channel_list{}));
	ASSERT_EQ (3, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out));
}

/*
 * An initial block broadcast reaches every principal representative and a fanout of the other peers, and the count returned covers both
 */
TEST (network, flood_block_initial)
{
	nano::test::system system (5);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 4);

	// One of the four peers is a principal representative
	auto const principal = node.network.find_node_id (system.nodes[1]->get_node_id ());
	ASSERT_NE (nullptr, principal);
	node.rep_crawler.force_add_rep (nano::dev::genesis_key.pub, principal);
	ASSERT_EQ (1, node.network.list_pr ().size ());
	ASSERT_EQ (principal, node.network.list_pr ().front ());

	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();

	// Publishes that arrived at the peers other than the representative
	auto received_by_others = [&] () {
		size_t count = 0;
		for (auto i = 2; i < 5; ++i)
		{
			count += system.nodes[i]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in);
		}
		return count;
	};

	// The representative plus a fanout of two among the other three, so one of them never gets it
	ASSERT_EQ (2, node.network.fanout ());
	ASSERT_EQ (3, node.network.flood_block_initial (block));
	ASSERT_TIMELY_EQ (5s, 1, system.nodes[1]->stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY_EQ (5s, 2, received_by_others ());
	ASSERT_ALWAYS_EQ (100ms, 2, received_by_others ());

	// It went out under the initial broadcast traffic type
	ASSERT_EQ (3, node.stats.count (nano::stat::type::tcp_channel_queued, nano::stat::detail::block_broadcast_initial, nano::stat::dir::out));
}

/*
 * A flood to all reaches every channel with room for the traffic type and skips the ones full for it
 */
TEST (network, flood_message_all)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);
	auto const type = nano::transport::traffic_type::test;
	nano::messages::keepalive const message{ nano::dev::network_params.network };

	// With room everywhere all three channels queue it
	ASSERT_EQ (3, node.network.flood_message_all (message, type));

	// Starve the node of outbound bandwidth, then push a queue's worth of large votes down one channel so its sending stalls with the queue full for the type
	node.outbound_limiter.reset (nano::rate_limit{ 1, 1.0 });
	auto const stalled = node.network.list ().front ();
	std::vector<nano::block_hash> hashes;
	for (uint64_t i = 1; i <= 200; ++i)
	{
		hashes.emplace_back (i);
	}
	nano::messages::confirm_ack const large{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, hashes) };
	for (size_t i = 0; i < nano::transport::tcp_channel::full_queue_size; ++i)
	{
		stalled->send (large, type);
	}
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::tcp_channel_wait, nano::stat::detail::wait_bandwidth, nano::stat::dir::out) >= 1);
	ASSERT_TRUE (stalled->max (type));

	// The stalled channel is skipped for that type, but still takes another type
	ASSERT_EQ (2, node.network.flood_message_all (message, type));
	ASSERT_EQ (2, node.network.flood_block_all (nano::dev::genesis, type));
	ASSERT_FALSE (stalled->max (nano::transport::traffic_type::keepalive));
	ASSERT_EQ (3, node.network.flood_message_all (message, nano::transport::traffic_type::keepalive));
}

/*
 * Blocks flooded many at a time go out one by one, the delay apart, and the callback runs once the last of them has gone
 */
TEST (network, flood_block_many)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);
	ASSERT_EQ (2, node.network.fanout ());

	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::block_builder builder;
	for (uint64_t i = 1; i <= 3; ++i)
	{
		blocks.push_back (builder
						  .send ()
						  .previous (i)
						  .destination (1)
						  .balance (2)
						  .sign (nano::keypair ().prv, 4)
						  .work (*system.work.generate (nano::root (i)))
						  .build ());
	}
	auto const sent = [&node] () {
		return node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::out);
	};

	// The first block goes out at once to a fanout of two, the rest wait for the delay
	nano::test::shared_flag done;
	node.network.flood_block_many (blocks, nano::transport::traffic_type::test, 250ms, [done] () { done.set (); });
	ASSERT_EQ (2, sent ());
	ASSERT_FALSE (done.is_set ());

	// The callback fires after the third block, by when every block has gone to its fanout
	ASSERT_TIMELY (5s, done.is_set ());
	ASSERT_EQ (6, sent ());
}

/*
 * Capacity is there when at least half of the target channels have room for the traffic type, the target capped at the connected peers,
 * and always for an empty target or a node without peers
 */
TEST (network, check_capacity)
{
	nano::test::system system (5);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 4);
	auto const type = nano::transport::traffic_type::test;
	auto const channels = node.network.list ();

	// Four free channels meet any target, one beyond the peer count counts as all four
	ASSERT_TRUE (node.network.check_capacity (type, 0));
	ASSERT_TRUE (node.network.check_capacity (type, 4));
	ASSERT_TRUE (node.network.check_capacity (type, 8));
	ASSERT_TRUE (node.network.check_capacity (type, 100));

	// With two channels full for the type the two free ones are still half of any target
	channels[0]->saturate (type);
	channels[1]->saturate (type);
	ASSERT_TRUE (node.network.check_capacity (type, 4));
	ASSERT_TRUE (node.network.check_capacity (type, 100));

	// With a third channel full the single free one is half of a target up to three, not of four or more
	channels[2]->saturate (type);
	ASSERT_TRUE (node.network.check_capacity (type, 3));
	ASSERT_FALSE (node.network.check_capacity (type, 4));
	ASSERT_FALSE (node.network.check_capacity (type, 100));

	// The fanout and ratio forms take their target from the peer count
	ASSERT_EQ (2, node.network.fanout ());
	ASSERT_TRUE (node.network.check_capacity_fanout (type));
	ASSERT_FALSE (node.network.check_capacity_fanout (type, 2.0f));
	ASSERT_TRUE (node.network.check_capacity_ratio (type, 0.5f));
	ASSERT_FALSE (node.network.check_capacity_ratio (type, 1.0f));

	// With every channel full there is no capacity for any target
	channels[3]->saturate (type);
	ASSERT_FALSE (node.network.check_capacity (type, 1));

	// A node without peers always has capacity
	auto & alone = *system.make_disconnected_node ();
	ASSERT_TRUE (alone.network.check_capacity (type, 100));
}

/*
 * Keepalive floods, random peers or this node's own endpoints, each reach a scaled fanout of the channels
 */
TEST (network, flood_keepalive)
{
	nano::test::system system (4);
	auto & node = *system.nodes[0];
	ASSERT_TIMELY_EQ (5s, node.network.size (), 3);
	ASSERT_EQ (2, node.network.fanout ());

	ASSERT_EQ (2, node.network.flood_keepalive ());
	ASSERT_EQ (1, node.network.flood_keepalive (0.5f));
	ASSERT_EQ (3, node.network.flood_keepalive (1.5f));
	ASSERT_EQ (1, node.network.flood_keepalive_self ());
	ASSERT_EQ (2, node.network.flood_keepalive_self (1.0f));
}

/*
 * A configured external address and port take the first slot of a self keepalive in place of the listening port
 */
TEST (network, fill_keepalive_self_external)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.external_address = "2001:db8::1";
	node_config.external_port = 7075;
	auto & node = *system.add_node (node_config);

	std::array<nano::endpoint, 8> target;
	node.network.fill_keepalive_self (target);
	ASSERT_EQ (nano::endpoint (boost::asio::ip::make_address_v6 ("2001:db8::1"), 7075), target[0]);
	ASSERT_EQ (nano::endpoint (boost::asio::ip::address_v6{}, 0), target[1]);
}

/*
 * A handshake response passes only when it signs the cookie its connection is waiting on with the node id it claims, never without such a cookie, from this node itself, for another genesis or in v1
 */
TEST (network, verify_handshake_response)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	nano::keypair peer_id;
	auto const genesis = node.network_params.ledger.genesis->hash ();
	using v2_payload = nano::messages::node_id_handshake::response_payload::v2_payload;

	// A response signed by the peer over the cookie of the query passes
	auto const query = node.network.prepare_handshake_query ();
	nano::messages::node_id_handshake::response_payload response;
	response.node_id = peer_id.pub;
	response.ext = v2_payload{ nano::random_pool::generate<nano::uint256_union> (), genesis };
	response.sign (query.cookie, peer_id);
	ASSERT_TRUE (node.network.verify_handshake_response (response, query.cookie));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));

	// The same response fails on a connection that has no cookie waiting for one
	ASSERT_FALSE (node.network.verify_handshake_response (response, std::nullopt));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::missing_cookie));

	// It fails against any other cookie too, the signature binds it to the one it answered
	ASSERT_FALSE (node.network.verify_handshake_response (response, nano::random_pool::generate<nano::uint256_union> ()));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_signature));

	// Another genesis fails before the cookie is looked at
	response.ext = v2_payload{ nano::random_pool::generate<nano::uint256_union> (), nano::block_hash{ 1 } };
	response.sign (query.cookie, peer_id);
	ASSERT_FALSE (node.network.verify_handshake_response (response, std::nullopt));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_genesis));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::missing_cookie));

	// This node's own id fails before anything else
	response.node_id = node.node_id.pub;
	response.sign (query.cookie, node.node_id);
	ASSERT_FALSE (node.network.verify_handshake_response (response, query.cookie));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_node_id));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_genesis));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));

	// A v1 response fails as an unsupported version, though its signature is valid
	nano::messages::node_id_handshake::response_payload v1_response;
	v1_response.node_id = peer_id.pub;
	v1_response.sign (query.cookie, peer_id);
	ASSERT_TRUE (std::holds_alternative<std::monostate> (v1_response.ext));
	ASSERT_TRUE (v1_response.validate (query.cookie));
	ASSERT_FALSE (node.network.verify_handshake_response (v1_response, query.cookie));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::unsupported_response_version));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_signature));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));
}

/*
 * A handshake response is signed over the query's cookie with this node's id and carries what its version allows: salt and genesis for v2, the capabilities on top for v3.
 * The node no longer answers in v1, so a query for a v1 response gets none
 */
TEST (network, prepare_handshake_response)
{
	nano::test::system system;
	nano::node_flags flags;
	flags.peering_only = true;
	auto & node = *system.add_node (flags);
	nano::messages::node_id_handshake::query_payload const query{ nano::random_pool::generate<nano::uint256_union> () };
	using handshake_version = nano::messages::handshake_version;
	using response_payload = nano::messages::node_id_handshake::response_payload;
	auto const genesis = node.network_params.ledger.genesis->hash ();

	// A query for a v1 response gets none, counted as an unsupported version
	auto const v1 = node.network.prepare_handshake_response (query, handshake_version::v1);
	ASSERT_FALSE (v1.has_value ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::unsupported_query_version));

	auto const v2 = node.network.prepare_handshake_response (query, handshake_version::v2).value ();
	ASSERT_EQ (node.node_id.pub, v2.node_id);
	ASSERT_TRUE (v2.validate (query.cookie));
	ASSERT_TRUE (std::holds_alternative<response_payload::v2_payload> (v2.ext));
	ASSERT_EQ (genesis, v2.genesis ());
	ASSERT_FALSE (v2.flags ().test (nano::node_capabilities::no_ledger));

	auto const v3 = node.network.prepare_handshake_response (query, handshake_version::v3).value ();
	ASSERT_EQ (node.node_id.pub, v3.node_id);
	ASSERT_TRUE (v3.validate (query.cookie));
	ASSERT_TRUE (std::holds_alternative<response_payload::v3_payload> (v3.ext));
	ASSERT_EQ (genesis, v3.genesis ());
	ASSERT_TRUE (v3.flags ().test (nano::node_capabilities::no_ledger));

	// The signature binds the response to the cookie it answered
	ASSERT_FALSE (v3.validate (nano::random_pool::generate<nano::uint256_union> ()));

	// Only the v1 query was refused
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::unsupported_query_version));
}

/*
 * Every handshake query carries a cookie of its own, so a response to one query never answers another
 */
TEST (network, prepare_handshake_query)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];

	// Two queries prepared back to back differ in their cookie
	auto const query1 = node.network.prepare_handshake_query ();
	auto const query2 = node.network.prepare_handshake_query ();
	ASSERT_FALSE (query1.cookie.is_zero ());
	ASSERT_FALSE (query2.cookie.is_zero ());
	ASSERT_NE (query1.cookie, query2.cookie);
}

/*
 * An entry of the duplicate filter keeps only the low bits of the epoch it went in at, and the network's cleanup loop clears the filter when those bits wrap, so no entry is left to read as recent a whole lap later
 * A recent entry shows it: its cutoff is a long way off, so only the clear takes it out
 */
TEST (network, wrap_duplicate_filter)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto & filter = node.network.filter;
	uint64_t const lap = nano::duplicate_filter::max_age_cutoff + 1;

	// Move the epoch to the start of a lap, one epoch at a time until an advance reports the wrap
	bool wrapped = false;
	while (!wrapped)
	{
		wrapped = filter.advance ();
	}

	// Stop two rounds of the cleanup loop short of the next wrap, so that a round running right now still leaves the wrap ahead
	ASSERT_FALSE (filter.advance (/* epochs */ lap - 2));

	// A message goes in, with the default cutoff far beyond those two rounds
	auto const digest = filter.hash (*nano::dev::genesis);
	ASSERT_TRUE (filter.insert (digest));
	ASSERT_TRUE (filter.contains (digest));

	// The loop takes the epoch over the wrap and clears the filter
	ASSERT_TIMELY (10s, !filter.contains (digest));
}

/*
 * Hidden nodes reach public nodes but never each other, even once keepalives introduce them
 */
TEST (network, topology_hidden_nodes)
{
	nano::test::system system;
	nano::test::topology topo{ system };
	auto relay = topo.add_public (system.default_config ());
	auto hidden1 = topo.add_hidden (system.default_config ());
	auto hidden2 = topo.add_hidden (system.default_config ());

	ASSERT_NO_ERROR (topo.connect (*hidden1, *relay));
	ASSERT_NO_ERROR (topo.connect (*hidden2, *relay));
	ASSERT_TRUE (nano::test::topology::connected (*hidden1, *relay));
	ASSERT_TRUE (nano::test::topology::connected (*hidden2, *relay));

	// The relay's keepalives introduce the hidden nodes to each other, their attempts are refused
	ASSERT_TIMELY (15s, hidden1->stats.count (nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted) + hidden2->stats.count (nano::stat::type::tcp_channels_rejected, nano::stat::detail::blocklisted) >= 1);
	ASSERT_FALSE (nano::test::topology::connected (*hidden1, *hidden2));
	ASSERT_EQ (nullptr, hidden1->network.find_node_id (hidden2->get_node_id ()));
	ASSERT_EQ (nullptr, hidden2->network.find_node_id (hidden1->get_node_id ()));
}
