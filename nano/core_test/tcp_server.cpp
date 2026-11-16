#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/lib/stream.hpp>
#include <nano/messages/confirm.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/publish.hpp>
#include <nano/messages/vote_relay.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/message_processor.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/peer.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <variant>

using namespace std::chrono_literals;

namespace
{
// A send from genesis with valid work, which no node knows until it is published
std::shared_ptr<nano::block> make_genesis_send (nano::test::system & system)
{
	return nano::state_block_builder ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::dev::genesis->hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (nano::keypair{}.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (*system.work.generate (nano::dev::genesis->hash ()))
	.build ();
}

// A legacy receive block with valid work on a previous no node has, so a node that receives it takes it no further
std::shared_ptr<nano::block> make_detached_receive (nano::test::system & system)
{
	nano::keypair key;
	auto const previous = nano::test::random_hash ();
	return nano::block_builder ()
	.receive ()
	.previous (previous)
	.source (nano::test::random_hash ())
	.sign (key.prv, key.pub)
	.work (*system.work.generate (previous))
	.build ();
}

// The serialized header alone, for a message a test cuts short or fills in by hand
std::vector<uint8_t> serialize_header (nano::messages::message_header const & header)
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };
		header.serialize (stream);
	}
	return bytes;
}
}

/*
 * A peer that connects and completes the two-step handshake gets a channel on the node under its node id.
 * The handshake is counted as passed once and none is aborted
 */
TEST (tcp_server, handshake_success)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_EQ (0, node.network.size ());

	// The peer asks, the node answers and asks back, and the peer's answer completes the handshake
	ASSERT_TRUE (peer.handshake ());

	// The node holds one TCP channel, under the peer's id
	ASSERT_EQ (1, node.network.size ());
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (nano::transport::transport_type::tcp, channel->get_type ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A peer that speaks the v2 handshake, as nodes from before the capability exchange do, is accepted as well.
 * The node answers it in v2, which carries no capabilities, and records none for the peer
 */
TEST (tcp_server, handshake_success_v2)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// The peer's query announces v2 only, and its response is a v2 one
	ASSERT_TRUE (peer.handshake (nano::test::handshake_v2 ()));

	// The node answered in v2
	auto const handshakes = peer.received<nano::messages::node_id_handshake> ();
	ASSERT_EQ (1, handshakes.size ());
	ASSERT_EQ (nano::messages::handshake_version::v2, handshakes.front ().version ());
	ASSERT_TRUE (handshakes.front ().response.has_value ());
	ASSERT_TRUE (std::holds_alternative<nano::messages::node_id_handshake::response_payload::v2_payload> (handshakes.front ().response->ext));

	// The node holds a channel under the peer's id, with no capabilities
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_FALSE (channel->get_peer_info ().capabilities.any ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A TCP channel records the protocol version its peer used in the handshake
 */
TEST (tcp_server, handshake_records_remote_protocol_version)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A peer on the oldest version the node still accepts, which speaks the v2 handshake
	auto const version = node.network_params.network.protocol_version_min;
	ASSERT_LT (version, node.network_params.network.protocol_version);
	ASSERT_TRUE (peer.handshake (nano::test::handshake_v2 ({ .protocol_version = version })));

	// The channel carries that version, and is listed for it but not for a newer one
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_EQ (version, channel->get_network_version ());
	ASSERT_EQ (1, node.network.tcp_channels.list (version).size ());
	ASSERT_TRUE (node.network.tcp_channels.list (version + 1).empty ());
}

/*
 * A message of a type outside the protocol, sent in place of the handshake, is refused at its header:
 * the server counts the invalid type, aborts the handshake and closes the connection without reading on
 */
TEST (tcp_server, handshake_unknown_message_type)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A header with an unknown type, followed by bytes that are never read
	nano::messages::message_header const header{ nano::dev::network_params.network, static_cast<nano::messages::message_type> (0xff) };
	auto bytes = serialize_header (header);
	bytes.resize (bytes.size () + 100, 0xab);
	ASSERT_TRUE (peer.send_bytes (bytes));

	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_message_type, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A peer that hangs up in the middle of a header fails the server's read, which stops the server with that socket error counted once.
 * The handshake was cut short rather than refused, so neither an aborted handshake nor an invalid message is counted
 */
TEST (tcp_server, handshake_incomplete_message)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// Half of a handshake header, then the peer closes the connection
	nano::messages::message_header const header{ nano::dev::network_params.network, nano::messages::message_type::node_id_handshake };
	auto const bytes = serialize_header (header);
	ASSERT_TRUE (peer.send_bytes (nano::buffer_view{ bytes }.first (4)));
	peer.close ();

	// The server stops on the failed read
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::tcp_server_error, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::dir::in));
}

/*
 * A valid message of another type sent before the handshake completes is refused at its header, before its payload is read,
 * and the handshake is aborted
 */
TEST (tcp_server, handshake_non_handshake_message)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A keepalive where the handshake should be
	ASSERT_TRUE (peer.send (nano::messages::keepalive{ nano::dev::network_params.network }));

	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_message_type, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
}

/*
 * A handshake response under the node's own id is refused as a connection of the node to itself
 */
TEST (tcp_server, handshake_self_connection_rejected)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A handshake signed with the node's own key
	auto const handshake = nano::test::handshake_v3 ({ .node_id = node.node_id });

	// The node answers the query like any other and asks back
	ASSERT_TRUE (peer.send (handshake.query ()));
	ASSERT_TIMELY_EQ (5s, 1, peer.received<nano::messages::node_id_handshake> ().size ());
	auto const reply = peer.received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (reply.query.has_value ());

	// The response under its own id is refused, and the node closes the connection
	ASSERT_TRUE (peer.send (handshake.response (reply.query->cookie)));
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_node_id));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
	ASSERT_EQ (0, node.network.size ());
}

/*
 * A connection gets one handshake query answered: a second one is a handshake error that ends the connection
 */
TEST (tcp_server, handshake_multiple_queries_rejected)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// Two queries in a row
	auto const handshake = nano::test::handshake_v3 ();
	ASSERT_TRUE (peer.send (handshake.query ()));
	ASSERT_TRUE (peer.send (handshake.query ()));

	// The node answered the first and refused the second
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, peer.received<nano::messages::node_id_handshake> ().size ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A handshake query from a protocol version below the minimum is refused at its header as outdated, and the handshake is aborted
 */
TEST (tcp_server, handshake_outdated_protocol_version)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A query from one version below the minimum
	auto const handshake = nano::test::handshake_v3 ();
	auto query = handshake.query ();
	query.header.version_using = node.network_params.network.protocol_version_min - 1;
	ASSERT_TRUE (peer.send (query));

	// The node closes the connection without answering
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_TRUE (peer.received<nano::messages::node_id_handshake> ().empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::outdated_version, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A handshake query whose header names another network is refused, and the handshake is aborted
 */
TEST (tcp_server, handshake_wrong_network_id)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A query stamped for the live network
	auto const handshake = nano::test::handshake_v3 ();
	auto query = handshake.query ();
	query.header.network = nano::network_type::nano_live_network;
	ASSERT_NE (query.header.network, node.network_params.network.current_network);
	ASSERT_TRUE (peer.send (query));

	// The node closes the connection without answering
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_TRUE (peer.received<nano::messages::node_id_handshake> ().empty ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_network, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}

/*
 * A publish header that names no block type declares no usable payload length, so the server refuses it at the header and drops the connection
 */
TEST (tcp_server, rejects_publish_without_block_type)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// The payload length of a publish follows from its block type, so this header declares none
	nano::messages::message_header header{ nano::dev::network_params.network, nano::messages::message_type::publish };
	header.block_type_set (nano::block_type::not_a_block);
	ASSERT_TRUE (peer.send_bytes (serialize_header (header)));

	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_header, nano::stat::dir::in));
}

/*
 * Verify that v3 handshake exchanges node capability flags between peers
 */
TEST (tcp_server, handshake_flags_exchanged)
{
	nano::test::system system;

	nano::node_flags flags1;
	flags1.capabilities_override = nano::node_capabilities_flags{ nano::node_capabilities::topo_index };
	auto node1 = system.add_node (flags1);

	nano::node_flags flags2;
	flags2.capabilities_override = nano::node_capabilities_flags{ nano::node_capabilities::vote_relay };
	auto node2 = system.add_node (flags2);

	node1->network.merge_peer (node2->network.endpoint ());

	ASSERT_TIMELY (10s, node1->network.find_node_id (node2->get_node_id ()));
	ASSERT_TIMELY (10s, node2->network.find_node_id (node1->get_node_id ()));

	// node1's channel to node2 should carry node2's capabilities (vote_relay)
	auto chan1 = node1->network.find_node_id (node2->get_node_id ());
	ASSERT_TRUE (chan1);
	ASSERT_TRUE (chan1->has_capability (nano::node_capabilities::vote_relay));
	ASSERT_FALSE (chan1->has_capability (nano::node_capabilities::topo_index));

	// node2's channel to node1 should carry node1's capabilities (topo_index)
	auto chan2 = node2->network.find_node_id (node1->get_node_id ());
	ASSERT_TRUE (chan2);
	ASSERT_TRUE (chan2->has_capability (nano::node_capabilities::topo_index));
	ASSERT_FALSE (chan2->has_capability (nano::node_capabilities::vote_relay));

	ASSERT_EQ (node1->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
	ASSERT_EQ (node2->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
}

/*
 * A peering-only node advertises the no_ledger capability; a full peer sees it as not serving a ledger
 */
TEST (tcp_server, handshake_peering_only_flag)
{
	nano::test::system system;

	auto node1 = system.add_node ();

	nano::node_flags peering_flags;
	peering_flags.peering_only = true;
	auto node2 = system.add_node (peering_flags);

	node1->network.merge_peer (node2->network.endpoint ());

	ASSERT_TIMELY (10s, node1->network.find_node_id (node2->get_node_id ()));
	ASSERT_TIMELY (10s, node2->network.find_node_id (node1->get_node_id ()));

	// node1's channel to the peering-only node2 reports it as not serving a ledger
	auto chan1 = node1->network.find_node_id (node2->get_node_id ());
	ASSERT_TRUE (chan1);
	ASSERT_TRUE (chan1->has_capability (nano::node_capabilities::no_ledger));
	ASSERT_FALSE (chan1->serves_ledger ());

	// node2's channel to the full node1 reports it as serving a ledger
	auto chan2 = node2->network.find_node_id (node1->get_node_id ());
	ASSERT_TRUE (chan2);
	ASSERT_FALSE (chan2->has_capability (nano::node_capabilities::no_ledger));
	ASSERT_TRUE (chan2->serves_ledger ());
}

/*
 * A connection a node starts completes the two-step handshake and leaves each side one TCP channel carrying the other's node id
 */
TEST (tcp_server, send_node_id_handshake_tcp)
{
	nano::test::system system (1);
	auto & node0 = *system.nodes[0];
	auto & node1 = *system.make_disconnected_node ();
	auto const handshakes = [] (nano::node & node) {
		return node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::node_id_handshake, nano::stat::dir::in);
	};
	auto const keepalives = [] (nano::node & node) {
		return node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in);
	};
	ASSERT_EQ (0, node0.network.size ());
	ASSERT_EQ (0, node1.network.size ());

	// The accepting side handles the query and then the response, the connecting side only the answer to its query
	ASSERT_TRUE (node0.network.tcp_channels.start_tcp (node1.network.endpoint ()));
	ASSERT_TIMELY (5s, handshakes (node1) >= 2);
	ASSERT_TIMELY (5s, handshakes (node0) >= 1);

	// Each side ends up with one TCP channel carrying the other's node id
	ASSERT_TIMELY_EQ (5s, 1, node0.network.size ());
	ASSERT_TIMELY_EQ (5s, 1, node1.network.size ());
	auto const channel0 = node0.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel0);
	ASSERT_EQ (nano::transport::transport_type::tcp, channel0->get_type ());
	auto const channel1 = node1.network.find_node_id (node0.get_node_id ());
	ASSERT_NE (nullptr, channel1);
	ASSERT_EQ (nano::transport::transport_type::tcp, channel1->get_type ());

	// Both greet the new channel with a keepalive
	ASSERT_TIMELY (5s, keepalives (node0) >= 1);
	ASSERT_TIMELY (5s, keepalives (node1) >= 1);
}

/*
 * A publish whose block carries too little work is refused by the server and counted as an insufficient work error
 */
TEST (tcp_server, send_insufficient_work)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// Work zero is not enough for any block
	auto const block = nano::block_builder ()
					   .send ()
					   .previous (0)
					   .destination (1)
					   .balance (20)
					   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					   .work (0)
					   .build ();
	ASSERT_TRUE (nano::dev::network_params.network.work.validate_entry (*block));

	// The server counts the rejection and passes nothing on
	ASSERT_TRUE (peer.send (nano::messages::publish{ nano::dev::network_params.network, block }));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::insufficient_work, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
}

/*
 * A message whose header names another network is refused as an invalid network error and its connection dropped
 */
TEST (tcp_server, filter_invalid_network_bytes)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// A keepalive stamped with the invalid network
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	keepalive.header.network = nano::network_type::invalid;
	ASSERT_TRUE (peer.send (keepalive));

	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_network, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
}

/*
 * A message from below the minimum protocol version is refused as an outdated version error and its connection dropped,
 * which ends the node's channel to the sender
 */
TEST (tcp_server, filter_invalid_version_using)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	auto const channel = node.network.find_node_id (peer.node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_TRUE (channel->alive ());

	// A keepalive claiming a version one below the minimum
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	keepalive.header.version_using = nano::dev::network_params.network.protocol_version_min - 1;
	ASSERT_TRUE (peer.send (keepalive));

	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::outdated_version, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	ASSERT_TIMELY (5s, !channel->alive ());
}

/*
 * A node whose message is refused as outdated finds the connection dropped by the receiver, so its own channel to the receiver dies
 */
TEST (tcp_server, depeer_on_outdated_version)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_TRUE (channel->alive ());

	// node2 sends a keepalive claiming a version one below the minimum
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	keepalive.header.version_using = nano::dev::network_params.network.protocol_version_min - 1;
	channel->send (keepalive, nano::transport::traffic_type::test);

	// node1 refuses it and drops the connection, which ends node2's channel
	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::outdated_version, nano::stat::dir::in));
	ASSERT_TIMELY (5s, !channel->alive ());
}

/*
 * A publish or vote sent before the handshake ends the connection without entering the duplicate filter, so the same message from an established peer is still received
 */
TEST (tcp_server, handshake_skips_duplicate_filter)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// A block and a vote for it, neither seen by the node yet
	auto const send = make_genesis_send (system);
	nano::messages::publish const publish{ nano::dev::network_params.network, send };
	nano::messages::confirm_ack const confirm_ack{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { send }) };

	// Each arrives on a connection of its own ahead of any handshake, and the server ends that connection without processing the message
	nano::test::peer early_publisher{ node };
	ASSERT_TRUE (early_publisher.send (publish));
	ASSERT_TIMELY (5s, !early_publisher.alive ());
	nano::test::peer early_voter{ node };
	ASSERT_TRUE (early_voter.send (confirm_ack));
	ASSERT_TIMELY (5s, !early_voter.alive ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));

	// The copies from a peer that completed the handshake are received rather than taken for duplicates
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TRUE (peer.send (confirm_ack));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * A publish or vote the message processor has no room for leaves the duplicate filter, so the next copy of it passes the filter instead of being taken for a duplicate
 */
TEST (tcp_server, dropped_message_leaves_duplicate_filter)
{
	nano::test::system system;
	// Without room in its processor queue the node drops every message it reads
	nano::node_config config = system.default_config ();
	config.message_processor->max_queue = 0;
	auto & node = *system.add_node (config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// A block and a vote for it
	auto const send = make_genesis_send (system);
	nano::messages::publish const publish{ nano::dev::network_params.network, send };
	nano::messages::confirm_ack const confirm_ack{ nano::dev::network_params.network, nano::test::make_vote (nano::dev::genesis_key, { send }) };

	// Two copies of each over one connection, read in order, so the second arrives after the first was dropped
	for (int i = 0; i < 2; ++i)
	{
		ASSERT_TRUE (peer.send (publish));
		ASSERT_TRUE (peer.send (confirm_ack));
	}

	// Every copy passed the filter and reached the full queue, none was taken for a duplicate
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message_processor_overfill, nano::stat::detail::publish));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message_processor_overfill, nano::stat::detail::confirm_ack));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * Realtime connections accept vote relay messages, and a node that takes no relay traffic drops each one under its message type
 */
TEST (tcp_server, vote_relay_messages_dropped)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// A request and an ack go out over the realtime connection
	nano::messages::vote_relay_req const request{ nano::dev::network_params.network, /* id */ 1, { { nano::dev::genesis->hash (), nano::dev::genesis->root () } } };
	ASSERT_TRUE (peer.send (request));
	nano::messages::vote_relay_ack const ack{ nano::dev::network_params.network, /* id */ 1, { nano::test::make_final_vote (nano::dev::genesis_key, { nano::dev::genesis->hash () }) } };
	ASSERT_TRUE (peer.send (ack));

	// Both pass the realtime filter and reach the dispatcher, which counts them as dropped by type; an ignored message would never get there
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::vote_relay_req, nano::stat::dir::in));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::vote_relay_ack, nano::stat::dir::in));
}

/*
 * A handshake response counts only on a connection that asked for one: a peer that opens with a response has no cookie of this node's to sign, so the node refuses it and closes the connection
 */
TEST (tcp_server, handshake_response_without_query)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// The peer opens with a response over a cookie of its own choosing
	auto const handshake = nano::test::handshake_v3 ();
	ASSERT_TRUE (peer.send (handshake.response (nano::random_pool::generate<nano::uint256_union> ())));

	// The node sent no query on this connection, so nothing answers the response and the connection goes
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::missing_cookie));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));
	ASSERT_EQ (nullptr, node.network.find_node_id (handshake.node_id));
}

/*
 * Every connection is asked with a cookie of its own, however many connections its IP has open, and a response counts only for the cookie of the connection it arrives on.
 * The per IP limit is set to one and switched off, so the listener takes two connections from loopback at once. One peer speaks the v3 handshake and the other v2
 */
TEST (tcp_server, handshake_cookie_per_connection)
{
	nano::test::system system;
	auto config = system.default_config ();
	config.network->max_peers_per_ip = 1;
	nano::node_flags flags;
	flags.disable_max_peers_per_ip = true;
	auto & node = *system.add_node (config, flags);

	// Two peers connect from the same IP and each opens the handshake with a query
	nano::test::peer peer1{ node };
	nano::test::peer peer2{ node };
	auto const handshake1 = nano::test::handshake_v3 ();
	auto const handshake2 = nano::test::handshake_v2 ();
	ASSERT_TRUE (peer1.send (handshake1.query ()));
	ASSERT_TRUE (peer2.send (handshake2.query ()));

	// The node answers both and asks each of them with a different cookie
	ASSERT_TIMELY_EQ (5s, 1, peer1.received<nano::messages::node_id_handshake> ().size ());
	ASSERT_TIMELY_EQ (5s, 1, peer2.received<nano::messages::node_id_handshake> ().size ());
	auto const reply1 = peer1.received<nano::messages::node_id_handshake> ().front ();
	auto const reply2 = peer2.received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (reply1.query.has_value ());
	ASSERT_TRUE (reply2.query.has_value ());
	ASSERT_NE (reply1.query->cookie, reply2.query->cookie);

	// A response over the other connection's cookie is refused and its connection closed
	ASSERT_TRUE (peer1.send (handshake1.response (reply2.query->cookie)));
	ASSERT_TIMELY (5s, !peer1.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_signature));
	ASSERT_EQ (nullptr, node.network.find_node_id (handshake1.node_id));

	// That refusal left the second connection's cookie alone, a response over it completes the handshake
	ASSERT_TRUE (peer2.send (handshake2.response (reply2.query->cookie)));
	ASSERT_TIMELY (5s, node.network.find_node_id (handshake2.node_id) != nullptr);
	ASSERT_EQ (1, node.stats.count (nano::stat::type::handshake, nano::stat::detail::ok));
}

/*
 * A connection to an endpoint opens with a query even when an earlier connection to that endpoint has just ended in the middle of its handshake
 */
TEST (tcp_server, handshake_query_after_reconnect)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	nano::test::peer_listener listener{ node };

	// The node connects to the listening peer and opens the handshake with a query
	ASSERT_TRUE (node.network.tcp_channels.start_tcp (listener.endpoint ()));
	auto const peer1 = listener.accept ();
	ASSERT_NE (nullptr, peer1);
	ASSERT_TIMELY_EQ (5s, 1, peer1->received<nano::messages::node_id_handshake> ().size ());
	auto const opening1 = peer1->received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (opening1.query.has_value ());

	// The peer hangs up without answering and the node lets go of the connection
	peer1->close ();
	ASSERT_TIMELY_EQ (5s, 0, node.tcp_listener.connection_count (nano::transport::connection_type::outbound));
	ASSERT_TIMELY_EQ (5s, 0, node.tcp_listener.attempt_count ());

	// A second connection to the same endpoint right away asks again, with a cookie of its own
	ASSERT_TRUE (node.network.tcp_channels.start_tcp (listener.endpoint ()));
	auto const peer2 = listener.accept ();
	ASSERT_NE (nullptr, peer2);
	ASSERT_TIMELY_EQ (5s, 1, peer2->received<nano::messages::node_id_handshake> ().size ());
	auto const opening2 = peer2->received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (opening2.query.has_value ());
	ASSERT_NE (opening1.query->cookie, opening2.query->cookie);
}

/*
 * A publish and a vote whose payloads are the same bytes are both received: the duplicate filter goes by what a message is as well as by its payload
 */
TEST (tcp_server, same_payload_passes_duplicate_filter)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// A receive block in the form a publish carries it
	auto const receive = make_detached_receive (system);
	std::vector<uint8_t> block_bytes;
	{
		nano::vectorstream stream{ block_bytes };
		receive->serialize (stream);
	}

	// The same bytes read as a vote with one hash
	bool error = false;
	nano::bufferstream stream{ block_bytes.data (), block_bytes.size () };
	auto const vote = std::make_shared<nano::vote> (error, stream);
	ASSERT_FALSE (error);
	ASSERT_EQ (1, vote->hashes.size ());

	// The two messages differ in their headers only
	nano::messages::publish const publish{ nano::dev::network_params.network, receive };
	nano::messages::confirm_ack const confirm_ack{ nano::dev::network_params.network, vote };
	auto const publish_bytes = publish.to_bytes ();
	auto const confirm_ack_bytes = confirm_ack.to_bytes ();
	auto const header_size = nano::messages::message_header::size;
	ASSERT_TRUE (std::equal (publish_bytes->begin () + header_size, publish_bytes->end (), confirm_ack_bytes->begin () + header_size, confirm_ack_bytes->end ()));

	// Both are received, neither is taken for a duplicate of the other
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TRUE (peer.send (confirm_ack));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * Only publishes and votes go through the duplicate filter: a repeat of any other message is received like the first
 */
TEST (tcp_server, other_messages_skip_duplicate_filter)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// The same vote relay request twice over one connection
	nano::messages::vote_relay_req const request{ nano::dev::network_params.network, /* id */ 1, { { nano::dev::genesis->hash (), nano::dev::genesis->root () } } };
	ASSERT_TRUE (peer.send (request));
	ASSERT_TRUE (peer.send (request));

	// Both reach the dispatcher, which counts each as dropped by a node that takes no relay traffic
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::vote_relay_req, nano::stat::dir::in));
}

/*
 * With deduplication turned off in the config, a repeat of a publish or a vote is received like the first
 */
TEST (tcp_server, disabled_duplicate_filter)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.network->duplicate_filter_enable = false;
	auto & node = *system.add_node (config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// A block and a vote that the node takes no further, so nothing but these sends reaches it
	nano::messages::publish const publish{ nano::dev::network_params.network, make_detached_receive (system) };
	nano::messages::confirm_ack const confirm_ack{ nano::dev::network_params.network, nano::test::make_vote (nano::keypair{}, { nano::test::random_hash () }) };

	// Two copies of each over one connection
	for (int i = 0; i < 2; ++i)
	{
		ASSERT_TRUE (peer.send (publish));
		ASSERT_TRUE (peer.send (confirm_ack));
	}

	// Every copy is received, none is taken for a duplicate
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message, nano::stat::detail::confirm_ack, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_confirm_ack_message));
}

/*
 * A publish the block processor refuses leaves the duplicate filter, so the next copy of it is read as a new message instead of being taken for a duplicate
 */
TEST (tcp_server, refused_publish_leaves_duplicate_filter)
{
	nano::test::system system;
	// The node's block processor takes no blocks from peers
	nano::node_config config = system.default_config ();
	config.block_processor->max_peer_queue = 0;
	auto & node = *system.add_node (config);
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	nano::messages::publish const publish{ nano::dev::network_params.network, make_genesis_send (system) };

	// The first copy passes the filter and is refused, which clears it from the filter before the drop is counted
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in));

	// The second copy gets as far as the first did, where a duplicate would have stopped at the filter
	ASSERT_TRUE (peer.send (publish));
	ASSERT_TIMELY_EQ (5s, 2, node.stats.count (nano::stat::type::message_drop, nano::stat::detail::publish, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::filter, nano::stat::detail::duplicate_publish_message));
}

/*
 * The node answers a handshake query with a response signed under its node id over the query's cookie, together with a query of its own
 */
TEST (tcp_server, handshake_query_answered)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// The peer opens the handshake with a query
	auto const handshake = nano::test::handshake_v3 ();
	auto const query = handshake.query ();
	ASSERT_TRUE (peer.send (query));

	// The node's reply proves its id over the query's cookie and names its genesis
	ASSERT_TIMELY_EQ (5s, 1, peer.received<nano::messages::node_id_handshake> ().size ());
	auto const reply = peer.received<nano::messages::node_id_handshake> ().front ();
	ASSERT_TRUE (reply.response.has_value ());
	ASSERT_EQ (node.node_id.pub, reply.response->node_id);
	ASSERT_TRUE (reply.response->validate (query.query->cookie));
	ASSERT_TRUE (reply.response->genesis ().has_value ());
	ASSERT_EQ (node.network_params.ledger.genesis->hash (), *reply.response->genesis ());

	// It asks the peer in turn, with a cookie of the node's own
	ASSERT_TRUE (reply.query.has_value ());
	ASSERT_NE (query.query->cookie, reply.query->cookie);
}

/*
 * A realtime message whose header declares the largest payload its length field allows is read in full into the server's buffer.
 * When that payload does not parse, the server counts the invalid message and ends the connection instead of reading on
 */
TEST (tcp_server, realtime_largest_payload)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());

	// An asc_pull_req header with every length bit set, followed by that many zero bytes, which name no valid request type
	nano::messages::message_header header{ nano::dev::network_params.network, nano::messages::message_type::asc_pull_req };
	header.extensions = nano::messages::message_header::extensions_bitset_t{ 0xffff };
	auto bytes = serialize_header (header);
	bytes.resize (bytes.size () + header.payload_length_bytes (), 0);
	ASSERT_TRUE (peer.send_bytes (bytes));

	// The node reads the whole payload, refuses it and closes the connection
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_asc_pull_req_message, nano::stat::dir::in));
}

/*
 * A realtime connection the peer closes fails the server's read for the next message, which stops the server with that socket error counted once and no invalid message counted
 */
TEST (tcp_server, realtime_socket_error)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server_error, nano::stat::dir::in));

	// The peer closes its end while the server waits for the next message
	peer.close ();

	// The server's read fails, which stops it
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::tcp_server_error, nano::stat::dir::in));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::dir::in));
}

/*
 * The node counts every handshake message it sends once: on a connection it accepted only its response, which carries its own query,
 * and on one it opened its opening query and its response
 */
TEST (tcp_server, handshake_messages_counted_once)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	// A peer connects and completes the handshake
	nano::test::peer inbound{ node };
	ASSERT_TRUE (inbound.handshake ());

	// The node sent only its response
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_initiate, nano::stat::dir::out));
	ASSERT_EQ (0, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_response, nano::stat::dir::out));

	// The node connects to a listening peer and completes that handshake too
	nano::test::peer_listener listener{ node };
	ASSERT_TRUE (node.network.tcp_channels.start_tcp (listener.endpoint ()));
	auto const outbound = listener.accept ();
	ASSERT_NE (nullptr, outbound);
	ASSERT_TRUE (outbound->handshake ());

	// It added its query and its response, each counted once
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_initiate, nano::stat::dir::out));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake, nano::stat::dir::out));
	ASSERT_EQ (2, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_response, nano::stat::dir::out));
}

/*
 * A handshake message arriving on a connection already in realtime mode is ignored: it is counted as such, gets no reply,
 * and the connection carries on with the messages that follow
 */
TEST (tcp_server, realtime_ignores_handshake)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };
	ASSERT_TRUE (peer.handshake ());
	auto const handshakes_before = peer.received<nano::messages::node_id_handshake> ().size ();

	// A fresh handshake query, then a keepalive behind it on the same connection
	ASSERT_TRUE (peer.send (nano::test::handshake_v3 ().query ()));
	ASSERT_TRUE (peer.send (nano::messages::keepalive{ nano::dev::network_params.network }));

	// The keepalive arrives, so the query before it was read and passed over without a reply
	ASSERT_TIMELY_EQ (5s, 1, node.stats.count (nano::stat::type::message, nano::stat::detail::keepalive, nano::stat::dir::in));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::message_ignored));
	ASSERT_EQ (handshakes_before, peer.received<nano::messages::node_id_handshake> ().size ());
	ASSERT_TRUE (peer.alive ());
}

/*
 * A handshake message carrying neither a query nor a response is a handshake error, and the server aborts the handshake and closes the connection
 */
TEST (tcp_server, handshake_empty_rejected)
{
	nano::test::system system;
	auto & node = *system.add_node ();
	nano::test::peer peer{ node };

	// A handshake with no query and no response
	ASSERT_TRUE (peer.send (nano::messages::node_id_handshake{ nano::dev::network_params.network }));

	// The node refuses it and closes the connection
	ASSERT_TIMELY (5s, !peer.alive ());
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_error));
	ASSERT_EQ (1, node.stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort));
}
