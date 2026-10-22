#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/messages/messages.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/election.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node_observers.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/repcrawler.hpp>
#include <nano/node/scheduler/component.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/node/transport/fake.hpp>
#include <nano/node/transport/inproc.hpp>
#include <nano/node/transport/tcp_config.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/node/vote_processor.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/network.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/iostreams/stream_buffer.hpp>
#include <boost/range/join.hpp>
#include <boost/thread.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace std::chrono_literals;

TEST (network, construction_with_specified_port)
{
	nano::test::system system{};
	auto const port = nano::test::speculatively_choose_a_free_tcp_bind_port ();
	ASSERT_NE (port, 0);
	auto const node = system.add_node ([&] { nano::node_config c; c.peering_port = port; return c; }());
	EXPECT_EQ (port, node->network.port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, construction_without_specified_port)
{
	nano::test::system system{};
	auto const node = system.add_node ();
	auto const port = node->network.port.load ();
	EXPECT_NE (0, port);
	EXPECT_EQ (port, node->network.endpoint ().port ());
	EXPECT_EQ (port, node->tcp_listener.endpoint ().port ());
}

TEST (network, last_contacted)
{
	nano::test::system system (1);

	auto node0 = system.nodes[0];
	ASSERT_EQ (0, node0->network.size ());

	nano::node_config node1_config = system.default_config ();
	node1_config.tcp->max_inbound_connections = 0; // Prevent ephemeral node1->node0 channel replacement with incoming connection
	auto node1 (std::make_shared<nano::node> (nano::unique_path (), node1_config, system.work, nano::node_flags{}));
	node1->start ();
	system.nodes.push_back (node1);

	auto channel1 = nano::test::establish_tcp (system, *node1, node0->network.endpoint ());
	ASSERT_NE (nullptr, channel1);
	ASSERT_TIMELY_EQ (5s, node0->network.size (), 1);

	// channel0 is the other side of channel1, same connection different endpoint
	auto channel0 = node0->network.tcp_channels.find_node_id (node1->get_node_id ());
	ASSERT_NE (nullptr, channel0);

	{
		// check that the endpoints are part of the same connection
		std::shared_ptr<nano::transport::tcp_socket> sock0 = channel0->socket;
		std::shared_ptr<nano::transport::tcp_socket> sock1 = channel1->socket;
		ASSERT_EQ (sock0->get_local_endpoint (), sock1->get_remote_endpoint ());
		ASSERT_EQ (sock1->get_local_endpoint (), sock0->get_remote_endpoint ());
	}

	// capture the state before and ensure the clock ticks at least once
	auto timestamp_before_keepalive = channel0->get_last_packet_received ();
	auto keepalive_count = node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	ASSERT_TIMELY (3s, std::chrono::steady_clock::now () > timestamp_before_keepalive);

	// send 3 keepalives
	// we need an extra keepalive to handle the race condition between the timestamp set and the counter increment
	// and we need one more keepalive to handle the possibility that there is a keepalive already in flight when we start the crucial part of the test
	// it is possible that there could be multiple keepalives in flight but we assume here that there will be no more than one in flight for the purposes of this test
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);
	node1->network.send_keepalive (channel1);

	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in) >= keepalive_count + 3);
	ASSERT_EQ (node0->network.size (), 1);
	auto timestamp_after_keepalive = channel0->get_last_packet_received ();
	ASSERT_GT (timestamp_after_keepalive, timestamp_before_keepalive);
}

TEST (network, send_discarded_publish)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (2)
				 .sign (nano::keypair ().prv, 4)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();
	{
		auto transaction = node1.ledger.tx_begin_read ();
		auto sent = node1.network.flood_block (block, nano::transport::traffic_type::test);
		ASSERT_EQ (1, sent);
		ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.any.account_head (transaction, nano::dev::genesis_key.pub));
		ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	auto transaction = node1.ledger.tx_begin_read ();
	ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.any.account_head (transaction, nano::dev::genesis_key.pub));
	ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
	nano::test::system system (2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (1)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::root (1)))
				 .build ();
	{
		auto transaction = node1.ledger.tx_begin_read ();
		auto sent = node1.network.flood_block (block, nano::transport::traffic_type::test);
		ASSERT_EQ (1, sent);
		ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.any.account_head (transaction, nano::dev::genesis_key.pub));
		ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
	}
	ASSERT_TIMELY (10s, node2.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	auto transaction = node1.ledger.tx_begin_read ();
	ASSERT_EQ (nano::dev::genesis->hash (), node1.ledger.any.account_head (transaction, nano::dev::genesis_key.pub));
	ASSERT_EQ (nano::dev::genesis->hash (), node2.latest (nano::dev::genesis_key.pub));
}

TEST (network, send_valid_publish)
{
	auto type = nano::transport::transport_type::tcp;
	nano::node_flags node_flags;
	nano::test::system system (2, type, node_flags);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
	nano::block_builder builder;
	auto block2 = builder
				  .send ()
				  .previous (latest1)
				  .destination (key2.pub)
				  .balance (50)
				  .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				  .work (*system.work.generate (latest1))
				  .build ();
	auto hash2 (block2->hash ());
	nano::block_hash latest2 (node2.latest (nano::dev::genesis_key.pub));
	node2.process_active (std::make_shared<nano::send_block> (*block2));
	ASSERT_TIMELY (10s, node1.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in) != 0);
	ASSERT_NE (hash2, latest2);
	ASSERT_TIMELY (10s, node2.latest (nano::dev::genesis_key.pub) != latest2);
	ASSERT_EQ (50, node2.balance (nano::dev::genesis_key.pub));
}

TEST (network, peer_max_tcp_attempts)
{
	nano::test::system system;

	// Add nodes that can accept TCP connection, but not node ID handshake
	nano::node_flags node_flags;
	node_flags.disable_connection_cleanup = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_ip = 3;
	auto node = system.add_node (node_config, node_flags);

	for (auto i = 0; i < node_config.network->max_peers_per_ip; ++i)
	{
		// Disable reachout from temporary nodes to avoid mixing outbound and inbound connections
		nano::node_config temp_config = system.default_config ();
		temp_config.network->peer_reachout = {};
		temp_config.network->cached_peer_reachout = {};
		auto temp_node = system.make_disconnected_node (temp_config, node_flags);
		ASSERT_TRUE (node->network.merge_peer (temp_node->network.endpoint ()));
	}

	ASSERT_TIMELY_EQ (15s, node->network.size (), node_config.network->max_peers_per_ip);
	ASSERT_FALSE (node->network.tcp_channels.track_reachout (nano::endpoint (node->network.endpoint ().address (), system.get_available_port ())));
	ASSERT_LE (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_ip, nano::stat::dir::out));
}

TEST (network, peer_max_tcp_attempts_subnetwork)
{
	nano::test::system system;

	nano::node_flags node_flags;
	node_flags.disable_max_peers_per_ip = true;
	nano::node_config node_config = system.default_config ();
	node_config.network->max_peers_per_subnetwork = 3;
	auto node = system.add_node (node_config, node_flags);

	for (auto i (0); i < node->config.network->max_peers_per_subnetwork; ++i)
	{
		auto address (boost::asio::ip::make_address_v6 (boost::asio::ip::v4_mapped, boost::asio::ip::address_v4 (0x7f000001 + i))); // 127.0.0.1 hex
		nano::endpoint endpoint (address, system.get_available_port ());
		ASSERT_TRUE (node->network.tcp_channels.track_reachout (endpoint));
	}

	ASSERT_EQ (0, node->network.size ());
	ASSERT_EQ (0, node->stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_subnetwork, nano::stat::dir::out));
	ASSERT_FALSE (node->network.tcp_channels.track_reachout (nano::endpoint (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"), system.get_available_port ())));
	ASSERT_EQ (1, node->stats.count (nano::stat::type::tcp, nano::stat::detail::max_per_subnetwork, nano::stat::dir::out));
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
}

// Send two publish messages and asserts that the duplication is detected.
TEST (network, duplicate_detection)
{
	nano::test::system system;
	nano::node_flags node_flags;
	auto & node0 = *system.add_node (node_flags);
	auto & node1 = *system.add_node (node_flags);
	nano::messages::publish publish{ nano::dev::network_params.network, nano::dev::genesis };

	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	tcp_channel->send (publish, nano::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message), 0);
	tcp_channel->send (publish, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message), 1);
}

TEST (network, duplicate_revert_publish)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.block_processor->max_peer_queue = 0;
	auto & node (*system.add_node (node_config));
	nano::messages::publish publish{ nano::dev::network_params.network, nano::dev::genesis };
	std::vector<uint8_t> bytes = message_payload_to_bytes (publish);
	// Add to the blocks filter
	// Should be cleared when dropping due to a full block processor, as long as the message has the optional digest attached
	// Test network.duplicate_detection ensures that the digest is attached when deserializing messages
	nano::uint128_t digest;
	ASSERT_FALSE (node.network.filter.apply (bytes.data (), bytes.size (), &digest));
	ASSERT_TRUE (node.network.filter.apply (bytes.data (), bytes.size ()));
	auto other_node (std::make_shared<nano::node> (system.get_available_port (), nano::unique_path (), system.work, nano::node_flags{}));
	other_node->start ();
	system.nodes.push_back (other_node);
	auto channel = nano::test::establish_tcp (system, *other_node, node.network.endpoint ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (0, publish.digest);
	node.inbound (publish, nano::test::fake_channel (node));
	ASSERT_TRUE (node.network.filter.apply (bytes.data (), bytes.size ()));
	publish.digest = digest;
	node.inbound (publish, nano::test::fake_channel (node));
	ASSERT_FALSE (node.network.filter.apply (bytes.data (), bytes.size ()));
}

TEST (network, duplicate_vote_detection)
{
	nano::test::system system;
	auto & node0 = *system.add_node ();
	auto & node1 = *system.add_node ();

	auto vote = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () });
	nano::messages::confirm_ack message{ nano::dev::network_params.network, vote };

	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
	tcp_channel->send (message, nano::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 0);
	tcp_channel->send (message, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 1);
}

// Ensures that the filter doesn't filter out votes that could not be queued for processing
TEST (network, duplicate_revert_vote)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.vote_processor->enable = false; // Do not drain queued votes
	node_config.vote_processor->max_non_pr_queue = 1;
	node_config.vote_processor->max_pr_queue = 1;
	auto & node0 = *system.add_node (node_config);
	auto & node1 = *system.add_node (node_config);

	auto vote1 = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }, 1);
	nano::messages::confirm_ack message1{ nano::dev::network_params.network, vote1 };
	auto bytes1 = message_payload_to_bytes (message1);

	auto vote2 = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }, 2);
	nano::messages::confirm_ack message2{ nano::dev::network_params.network, vote2 };
	auto bytes2 = message_payload_to_bytes (message2);

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	// First vote should be processed
	tcp_channel->send (message1, nano::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 0);
	ASSERT_TIMELY (5s, node1.network.filter.check (bytes1.data (), bytes1.size ()));

	// Second vote should get dropped from processor queue
	tcp_channel->send (message2, nano::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 0);
	// And the filter should not have it
	WAIT (500ms); // Give the node time to process the vote
	ASSERT_TIMELY (5s, !node1.network.filter.check (bytes2.data (), bytes2.size ()));
}

TEST (network, expire_duplicate_filter)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.network->duplicate_filter_cutoff = 3; // Expire after 3 seconds
	auto & node0 = *system.add_node (node_config);
	auto & node1 = *system.add_node (node_config);

	auto vote = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () });
	nano::messages::confirm_ack message{ nano::dev::network_params.network, vote };
	auto bytes = message_payload_to_bytes (message);

	// Publish duplicate detection through TCP
	auto tcp_channel = node0.network.tcp_channels.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, tcp_channel);

	// Send a vote
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
	tcp_channel->send (message, nano::transport::traffic_type::test);
	ASSERT_ALWAYS_EQ (100ms, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 0);
	tcp_channel->send (message, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, node1.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message), 1);

	// The filter should expire the vote after some time
	ASSERT_TRUE (node1.network.filter.check (bytes.data (), bytes.size ()));
	ASSERT_TIMELY (10s, !node1.network.filter.check (bytes.data (), bytes.size ()));
}

TEST (network, tcp_no_accept_excluded_peers)
{
	nano::test::system system (1);
	auto node0 (system.nodes[0]);
	ASSERT_EQ (0, node0->network.size ());
	auto node1 (std::make_shared<nano::node> (system.get_available_port (), nano::unique_path (), system.work, nano::node_flags{}));
	node1->start ();
	system.nodes.push_back (node1);
	auto endpoint1_tcp (nano::transport::map_endpoint_to_tcp (node1->network.endpoint ()));
	while (!node0->network.excluded_peers.check (endpoint1_tcp))
	{
		node0->network.excluded_peers.add (endpoint1_tcp);
	}
	ASSERT_EQ (0, node0->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::excluded));
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY (5s, node0->stats.count (nano::stat::type::tcp_listener_rejected, nano::stat::detail::excluded) >= 1);
	ASSERT_EQ (nullptr, node0->network.find_node_id (node1->get_node_id ()));

	// Should not actively reachout to excluded peers
	ASSERT_FALSE (node0->network.track_reachout (node1->network.endpoint ()));

	// Erasing from excluded peers should allow a connection
	node0->network.excluded_peers.remove (endpoint1_tcp);
	ASSERT_FALSE (node0->network.excluded_peers.check (endpoint1_tcp));

	// Wait until there is a syn_cookie
	ASSERT_TIMELY (5s, node1->network.syn_cookies.cookies_size () != 0);

	// Manually cleanup previous attempt
	node1->network.cleanup (std::chrono::steady_clock::now ());
	node1->network.syn_cookies.purge (std::chrono::steady_clock::now ());

	// Ensure a successful connection
	ASSERT_EQ (0, node0->network.size ());
	node1->network.merge_peer (node0->network.endpoint ());
	ASSERT_TIMELY_EQ (5s, node0->network.size (), 1);
}

TEST (network, cleanup_purge)
{
	auto test_start = std::chrono::steady_clock::now ();

	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);

	auto node2 (std::make_shared<nano::node> (system.get_available_port (), nano::unique_path (), system.work, nano::node_flags{}));
	node2->start ();
	system.nodes.push_back (node2);

	ASSERT_EQ (0, node1.network.size ());
	node1.network.cleanup (test_start);
	ASSERT_EQ (0, node1.network.size ());

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (0, node1.network.size ());

	node1.network.merge_peer (node2->network.endpoint ());

	ASSERT_TIMELY_EQ (5s, node1.network.size (), 1);

	node1.network.cleanup (test_start);
	ASSERT_EQ (1, node1.network.size ());
	ASSERT_EQ (0, node1.stats.count (nano::stat::type::tcp_channels_purge));

	node1.network.cleanup (std::chrono::steady_clock::now ());
	ASSERT_EQ (1, node1.stats.count (nano::stat::type::tcp_channels_purge, nano::stat::detail::idle));
}

TEST (network, fill_keepalive_self)
{
	nano::test::system system{ 2 };

	auto get_keepalive = [&system] (nano::node & node) {
		std::array<nano::endpoint, 8> target;
		node.network.fill_keepalive_self (target);
		return target;
	};

	ASSERT_TIMELY_EQ (5s, get_keepalive (system.node (0))[2].port (), system.nodes[1]->network.port);
}

/*
 * Tests that channel and channel container removes channels with dead local sockets
 */
TEST (network, purge_dead_channel)
{
	nano::test::system system;

	nano::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);

	node1.observers.socket_connected.add ([&] (auto const & socket) {
		system.logger.debug (nano::log::type::test, "Connected socket: {}", nano::streamed (socket));
	});

	auto & node2 = *system.add_node (flags);

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_ALWAYS_EQ (500ms, node1.network.size (), 1);

	// Store reference to the only channel
	auto channels = node1.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	std::deque<std::shared_ptr<nano::transport::tcp_socket>> sockets;
	ASSERT_TIMELY_EQ (5s, (sockets = node1.tcp_listener.all_sockets ()).size (), 1);
	auto socket = sockets.front ();
	ASSERT_TRUE (socket);

	// When socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	socket->close ();
	ASSERT_TIMELY (10s, !channel->alive ());

	auto channel_exists = [] (auto & node, auto & channel) {
		auto channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};
	ASSERT_TIMELY (5s, !channel_exists (node1, channel));
}

/*
 * Tests that channel and channel container removes channels with dead remote sockets
 */
TEST (network, purge_dead_channel_remote)
{
	nano::test::system system;

	nano::node_flags flags;
	// Disable non realtime sockets
	flags.disable_bootstrap_bulk_push_client = true;
	flags.disable_bootstrap_bulk_pull_server = true;
	flags.disable_bootstrap_listener = true;
	flags.disable_lazy_bootstrap = true;
	flags.disable_legacy_bootstrap = true;
	flags.disable_wallet_bootstrap = true;

	auto & node1 = *system.add_node (flags);
	auto & node2 = *system.add_node (flags);

	node2.observers.socket_connected.add ([&] (auto const & socket) {
		system.logger.debug (nano::log::type::test, "Connected socket: {}", nano::streamed (socket));
	});

	ASSERT_EQ (node1.network.size (), 1);
	ASSERT_EQ (node2.network.size (), 1);
	ASSERT_ALWAYS_EQ (500ms, std::min (node1.network.size (), node2.network.size ()), 1);

	// Store reference to the only channel
	auto channels = node2.network.list ();
	ASSERT_EQ (channels.size (), 1);
	auto channel = channels.front ();
	ASSERT_TRUE (channel);

	std::deque<std::shared_ptr<nano::transport::tcp_socket>> sockets;
	ASSERT_TIMELY_EQ (5s, (sockets = node1.tcp_listener.all_sockets ()).size (), 1);
	auto socket = sockets.front ();
	ASSERT_TRUE (socket);

	// When remote socket is dead ensure channel knows about that
	ASSERT_TRUE (channel->alive ());
	socket->close ();
	ASSERT_TIMELY (5s, !channel->alive ());

	auto channel_exists = [] (auto & node, auto & channel) {
		auto channels = node.network.list ();
		return std::find (channels.begin (), channels.end (), channel) != channels.end ();
	};
	ASSERT_TIMELY (5s, !channel_exists (node2, channel));
}

TEST (network, flood_vote)
{
	nano::test::system system{ 4 };

	auto & node = *system.nodes[0];

	// Make one of the nodes a representative
	system.wallet (1)->insert_adhoc (nano::dev::genesis_key.prv);
	ASSERT_TIMELY_EQ (5s, node.rep_crawler.representative_count (), 1);

	auto vote = nano::test::make_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () });
	ASSERT_EQ (2, node.network.flood_vote (vote, nano::transport::traffic_type::test));
	ASSERT_EQ (3, node.network.flood_vote_all (vote, nano::transport::traffic_type::test));
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
	auto node_c = system.make_disconnected_node (system.default_config (), peering_flags);

	auto node_a = system.make_disconnected_node ();
	auto node_b = system.make_disconnected_node ();

	ASSERT_EQ (nullptr, node_a->network.find_node_id (node_b->get_node_id ()));
	ASSERT_EQ (nullptr, node_b->network.find_node_id (node_a->get_node_id ()));

	node_a->network.merge_peer (node_c->network.endpoint ());
	node_b->network.merge_peer (node_c->network.endpoint ());

	ASSERT_TIMELY (10s, node_a->network.find_node_id (node_c->get_node_id ()));
	ASSERT_TIMELY (10s, node_b->network.find_node_id (node_c->get_node_id ()));

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
