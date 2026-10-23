#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/node_capabilities.hpp>
#include <nano/messages/keepalive.hpp>
#include <nano/messages/message_visitor.hpp>
#include <nano/messages/node_id_handshake.hpp>
#include <nano/messages/publish.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/tcp_listener.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * Test case for valid handshake to ensure normal operation still works
 * TODO: This is very non tcp_server specific, should be rewritten
 */
TEST (tcp_server, handshake_success)
{
	nano::test::system system;
	auto node1 = system.add_node ();
	auto node2 = system.add_node ();

	// Establish connection between nodes
	node1->network.merge_peer (node2->network.endpoint ());

	// Wait for handshake to complete successfully
	ASSERT_TIMELY (10s, node1->network.find_node_id (node2->get_node_id ()));
	ASSERT_TIMELY (10s, node2->network.find_node_id (node1->get_node_id ()));

	// Verify no handshake aborts occurred
	ASSERT_EQ (node1->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
	ASSERT_EQ (node2->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
}

/*
 * A TCP channel records the remote protocol version from the handshake
 */
TEST (tcp_server, handshake_records_remote_protocol_version)
{
	nano::test::system system;
	auto node = system.add_node ();
	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);
	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	auto const remote_protocol_version = node->network_params.network.protocol_version_min;
	ASSERT_LT (remote_protocol_version, node->network_params.network.protocol_version);

	// Begin a normal two-step handshake while advertising an older, still-supported protocol version.
	nano::messages::node_id_handshake::query_payload initial_query{ nano::random_pool::generate<nano::uint256_union> () };
	nano::messages::node_id_handshake initial_handshake{ node->network_params.network, initial_query };
	initial_handshake.header.version_max = remote_protocol_version;
	initial_handshake.header.version_using = remote_protocol_version;
	initial_handshake.header.version_min = remote_protocol_version;

	auto initial_buffer = initial_handshake.to_shared_const_buffer ();
	auto [initial_write_ec, initial_bytes_written] = client_socket->blocking_write (initial_buffer, initial_buffer.size ());
	ASSERT_FALSE (initial_write_ec);
	ASSERT_EQ (initial_bytes_written, initial_buffer.size ());

	// Read the node's response and extract its query, which the simulated remote peer must sign.
	auto response_header_buffer = std::make_shared<std::vector<uint8_t>> (nano::messages::message_header::size);
	auto [header_read_ec, header_bytes_read] = client_socket->blocking_read (response_header_buffer, response_header_buffer->size ());
	ASSERT_FALSE (header_read_ec);
	ASSERT_EQ (header_bytes_read, response_header_buffer->size ());

	bool header_error = false;
	nano::bufferstream response_header_stream{ response_header_buffer->data (), response_header_buffer->size () };
	nano::messages::message_header response_header{ header_error, response_header_stream };
	ASSERT_FALSE (header_error);
	ASSERT_EQ (response_header.type, nano::messages::message_type::node_id_handshake);

	auto response_payload_buffer = std::make_shared<std::vector<uint8_t>> (response_header.payload_length_bytes ());
	auto [payload_read_ec, payload_bytes_read] = client_socket->blocking_read (response_payload_buffer, response_payload_buffer->size ());
	ASSERT_FALSE (payload_read_ec);
	ASSERT_EQ (payload_bytes_read, response_payload_buffer->size ());

	bool response_error = false;
	nano::bufferstream response_payload_stream{ response_payload_buffer->data (), response_payload_buffer->size () };
	nano::messages::node_id_handshake node_handshake{ response_error, response_payload_stream, response_header };
	ASSERT_FALSE (response_error);
	ASSERT_TRUE (node_handshake.query);

	nano::keypair remote_node_id;
	nano::messages::node_id_handshake::response_payload response;
	response.node_id = remote_node_id.pub;
	nano::messages::node_id_handshake::response_payload::v3_payload response_v3;
	response_v3.salt = nano::random_pool::generate<nano::uint256_union> ();
	response_v3.genesis = node->network_params.ledger.genesis->hash ();
	response.ext = response_v3;
	response.sign (node_handshake.query->cookie, remote_node_id);

	nano::messages::node_id_handshake final_handshake{ node->network_params.network, std::nullopt, response };
	final_handshake.header.version_max = remote_protocol_version;
	final_handshake.header.version_using = remote_protocol_version;
	final_handshake.header.version_min = remote_protocol_version;

	auto final_buffer = final_handshake.to_shared_const_buffer ();
	auto [final_write_ec, final_bytes_written] = client_socket->blocking_write (final_buffer, final_buffer.size ());
	ASSERT_FALSE (final_write_ec);
	ASSERT_EQ (final_bytes_written, final_buffer.size ());

	ASSERT_TIMELY (5s, node->network.find_node_id (remote_node_id.pub));
	auto channel = node->network.find_node_id (remote_node_id.pub);
	ASSERT_TRUE (channel);
	ASSERT_EQ (channel->get_network_version (), remote_protocol_version);
	ASSERT_EQ (node->network.tcp_channels.list (remote_protocol_version).size (), 1);
	ASSERT_TRUE (node->network.tcp_channels.list (remote_protocol_version + 1).empty ());
}

/*
 * This test verifies that when a tcp_server receives a malformed handshake message
 * that fails deserialization, it properly aborts the connection instead of continuing
 * processing with an invalid message.
 */
TEST (tcp_server, handshake_deserialization_failure)
{
	nano::test::system system;
	auto node = system.add_node ();

	// Create a client socket to connect to the node
	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect using blocking helper
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	// Ensure the server has accepted the connection
	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Send malformed handshake data that will fail deserialization
	// Create an invalid message header with wrong message type and invalid size
	std::vector<uint8_t> malformed_data;

	nano::messages::message_header header (nano::dev::network_params.network, static_cast<nano::messages::message_type> (0xFF)); // Invalid message type
	header.version_using = 0x12; // Some version
	header.version_min = 0x01;
	header.extensions = 0;

	// Serialize the header
	{
		nano::vectorstream stream (malformed_data);
		header.serialize (stream);
	}

	// Add some garbage payload that won't deserialize properly
	std::vector<uint8_t> garbage_payload (100, 0xAB);
	malformed_data.insert (malformed_data.end (), garbage_payload.begin (), garbage_payload.end ());

	// Send the malformed data using blocking write
	auto buffer = std::make_shared<std::vector<uint8_t>> (malformed_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, malformed_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, malformed_data.size ());

	// The server should close the connection after receiving malformed data
	// Wait for the connection to be dropped
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify stats show the handshake was aborted
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 1);
}

/*
 * Test case for partial/incomplete handshake message
 * This simulates a connection that sends only part of a handshake message
 */
TEST (tcp_server, handshake_incomplete_message)
{
	nano::test::system system;
	auto node = system.add_node ();

	// Create a client socket
	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect using blocking helper
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Send only partial header (not enough bytes for a complete header)
	std::vector<uint8_t> partial_header;
	nano::messages::message_header header (nano::dev::network_params.network, nano::messages::message_type::node_id_handshake);

	// Serialize header but only send first few bytes
	{
		nano::vectorstream stream (partial_header);
		header.serialize (stream);
	}

	// Send only first 4 bytes of header (incomplete)
	std::vector<uint8_t> incomplete_data (partial_header.begin (), partial_header.begin () + 4);

	auto buffer = std::make_shared<std::vector<uint8_t>> (incomplete_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, incomplete_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, incomplete_data.size ());

	// Close the client socket to signal EOF
	client_socket->close ();

	// Server should detect incomplete message and close connection
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());
}

TEST (tcp_server, handshake_invalid_message_type_aborts)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect to the node
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Create a valid header but with an invalid message type for handshake phase
	std::vector<uint8_t> invalid_handshake_data;

	// Use a valid message type that's not a handshake (e.g., keepalive)
	nano::messages::message_header header (nano::dev::network_params.network, nano::messages::message_type::keepalive);
	header.version_using = nano::dev::network_params.network.protocol_version;
	header.version_min = nano::dev::network_params.network.protocol_version_min;
	header.extensions = 0;

	// Serialize the header
	{
		nano::vectorstream stream (invalid_handshake_data);
		header.serialize (stream);
	}

	// Add a valid keepalive message body
	nano::messages::keepalive message (nano::dev::network_params.network);
	{
		nano::vectorstream stream (invalid_handshake_data);
		message.serialize (stream);
	}

	// Send the non-handshake message during handshake phase
	auto buffer = std::make_shared<std::vector<uint8_t>> (invalid_handshake_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, invalid_handshake_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, invalid_handshake_data.size ());

	// The server should abort the connection because it received a non-handshake message during handshake
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify the handshake was aborted
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 1);
}

/*
 * Test that a node rejects a connection from itself (same node ID)
 */
TEST (tcp_server, handshake_self_connection_rejected)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect to the node
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Create a handshake response claiming to be from the same node
	nano::messages::node_id_handshake::query_payload query{ nano::random_pool::generate<nano::uint256_union> () };

	nano::messages::node_id_handshake::response_payload response;
	response.node_id = node->node_id.pub; // Use our own node ID
	response.ext = nano::messages::node_id_handshake::response_payload::v2_payload{
		nano::random_pool::generate<nano::uint256_union> (), // salt
		node->network_params.ledger.genesis->hash () // genesis
	};
	response.sign (query.cookie, node->node_id); // Sign with our own key

	nano::messages::node_id_handshake handshake_msg (node->network_params.network, query, response);

	// Send the handshake with our own node ID
	auto shared_const_buffer = handshake_msg.to_shared_const_buffer ();
	auto [write_ec, bytes_written] = client_socket->blocking_write (shared_const_buffer, shared_const_buffer.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, shared_const_buffer.size ());

	// The server should reject the connection due to self-connection
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify the handshake was rejected
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::handshake, nano::stat::detail::invalid_node_id), 1);
}

/*
 * Test that multiple handshake queries are rejected (only one query allowed per connection)
 */
TEST (tcp_server, handshake_multiple_queries_rejected)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect to node1
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Send first handshake query
	nano::messages::node_id_handshake::query_payload query1{ nano::random_pool::generate<nano::uint256_union> () };
	nano::messages::node_id_handshake handshake1 (node->network_params.network, query1);

	auto buffer1 = handshake1.to_shared_const_buffer ();
	auto [write_ec1, bytes_written1] = client_socket->blocking_write (buffer1, buffer1.size ());
	ASSERT_FALSE (write_ec1);
	ASSERT_EQ (bytes_written1, buffer1.size ());

	// Send second handshake query (should be rejected)
	nano::messages::node_id_handshake::query_payload query2{ nano::random_pool::generate<nano::uint256_union> () };
	nano::messages::node_id_handshake handshake2 (node->network_params.network, query2);

	auto buffer2 = handshake2.to_shared_const_buffer ();
	auto [write_ec2, bytes_written2] = client_socket->blocking_write (buffer2, buffer2.size ());
	ASSERT_FALSE (write_ec2);
	ASSERT_EQ (bytes_written2, buffer2.size ());

	// The server should abort the connection due to multiple queries
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify the handshake was aborted due to multiple queries
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_error), 1);
}

/*
 * Test that messages with outdated protocol version are rejected
 */
TEST (tcp_server, handshake_outdated_protocol_version)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect to the node
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Create a handshake message with outdated protocol version
	std::vector<uint8_t> handshake_data;

	nano::messages::message_header header (nano::dev::network_params.network, nano::messages::message_type::node_id_handshake);
	header.version_using = node->network_params.network.protocol_version_min - 1; // Use version below minimum
	header.version_min = node->network_params.network.protocol_version_min - 1;
	header.extensions = 0;

	// Serialize the header
	{
		nano::vectorstream stream (handshake_data);
		header.serialize (stream);
	}

	// Add a basic handshake payload
	nano::messages::node_id_handshake::query_payload query{ nano::random_pool::generate<nano::uint256_union> () };
	nano::messages::node_id_handshake handshake_msg (node->network_params.network, query);
	{
		nano::vectorstream stream (handshake_data);
		handshake_msg.serialize (stream);
	}

	// Send the handshake with outdated protocol version
	auto buffer = std::make_shared<std::vector<uint8_t>> (handshake_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, handshake_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, handshake_data.size ());

	// The server should reject the connection due to outdated protocol version
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify the handshake was aborted
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 1);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::outdated_version), 1);
}

/*
 * Test that messages with wrong network ID are rejected
 */
TEST (tcp_server, handshake_wrong_network_id)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);

	// Connect to the node
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);

	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// Create a handshake message with wrong network ID
	std::vector<uint8_t> handshake_data;

	// Use a different network ID - create a different network_constants
	nano::network_type wrong_network = (node->network_params.network.current_network == nano::network_type::nano_live_network)
	? nano::network_type::nano_beta_network
	: nano::network_type::nano_live_network;
	nano::network_constants wrong_network_constants (nano::work_thresholds::publish_dev, wrong_network);

	nano::messages::message_header header (wrong_network_constants, nano::messages::message_type::node_id_handshake);
	header.version_using = node->network_params.network.protocol_version;
	header.version_min = node->network_params.network.protocol_version_min;
	header.extensions = 0;

	// Serialize the header
	{
		nano::vectorstream stream (handshake_data);
		header.serialize (stream);
	}

	// Add a basic handshake payload
	nano::messages::node_id_handshake::query_payload query{ nano::random_pool::generate<nano::uint256_union> () };
	nano::messages::node_id_handshake handshake_msg (node->network_params.network, query);
	{
		nano::vectorstream stream (handshake_data);
		handshake_msg.serialize (stream);
	}

	// Send the handshake with wrong network ID
	auto buffer = std::make_shared<std::vector<uint8_t>> (handshake_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, handshake_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, handshake_data.size ());

	// The server should reject the connection due to wrong network ID
	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());

	// Verify the handshake was aborted
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 1);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_network), 1);
}

/*
 * A publish header that names no block type is rejected before any payload is read and the connection is dropped
 */
TEST (tcp_server, rejects_publish_without_block_type)
{
	nano::test::system system;
	auto node = system.add_node ();

	auto client_socket = std::make_shared<nano::transport::tcp_socket> (*node);
	auto ec = client_socket->blocking_connect (node->network.endpoint ());
	ASSERT_FALSE (ec);
	ASSERT_TIMELY_EQ (5s, node->tcp_listener.all_sockets ().size (), 1);

	// The payload length of a publish follows from its block type, so this header declares no usable length
	nano::messages::message_header header (nano::dev::network_params.network, nano::messages::message_type::publish);
	header.block_type_set (nano::block_type::not_a_block);
	std::vector<uint8_t> header_data;
	{
		nano::vectorstream stream (header_data);
		header.serialize (stream);
	}

	auto buffer = std::make_shared<std::vector<uint8_t>> (header_data);
	auto [write_ec, bytes_written] = client_socket->blocking_write (buffer, header_data.size ());
	ASSERT_FALSE (write_ec);
	ASSERT_EQ (bytes_written, header_data.size ());

	ASSERT_TIMELY (5s, node->tcp_listener.all_sockets ().empty ());
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_header), 1);
	ASSERT_TIMELY_EQ (5s, node->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 1);
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
 * A publish whose block carries too little work is refused by the receiving server and counted as an insufficient work error
 */
TEST (tcp_server, send_insufficient_work)
{
	nano::test::system system (2);
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const channel = node1.network.find_node_id (node2.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// Work zero is not enough for any block
	nano::block_builder builder;
	auto block = builder
				 .send ()
				 .previous (0)
				 .destination (1)
				 .balance (20)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();
	ASSERT_TRUE (nano::dev::network_params.network.work.validate_entry (*block));

	// The receiving server counts the rejection
	channel->send (nano::messages::publish{ nano::dev::network_params.network, block }, nano::transport::traffic_type::test);
	ASSERT_TIMELY_EQ (5s, 1, node2.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::insufficient_work));
}

/*
 * A message whose header names another network is refused as an invalid network error
 */
TEST (tcp_server, filter_invalid_network_bytes)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// A keepalive stamped with the invalid network, written through a cast since the header field is const
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<nano::network_type &> (keepalive.header.network) = nano::network_type::invalid;
	channel->send (keepalive, nano::transport::traffic_type::test);

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::invalid_network));
}

/*
 * A message from below the minimum protocol version is refused as an outdated version error
 */
TEST (tcp_server, filter_invalid_version_using)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);

	// A keepalive claiming a version one below the minimum, written through a cast since the header field is const
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<uint8_t &> (keepalive.header.version_using) = nano::dev::network_params.network.protocol_version_min - 1;
	channel->send (keepalive, nano::transport::traffic_type::test);

	ASSERT_TIMELY_EQ (5s, 1, node1.stats.count (nano::stat::type::tcp_server_message_error, nano::stat::detail::outdated_version));
}

/*
 * A message from below the minimum protocol version gets the connection dropped, so the sender's channel dies with it
 */
TEST (tcp_server, depeer_on_outdated_version)
{
	nano::test::system system{ 2 };
	auto & node1 = *system.nodes[0];
	auto & node2 = *system.nodes[1];
	auto const channel = node2.network.find_node_id (node1.get_node_id ());
	ASSERT_NE (nullptr, channel);
	ASSERT_TRUE (channel->alive ());

	// A keepalive claiming a version one below the minimum, written through a cast since the header field is const
	nano::messages::keepalive keepalive{ nano::dev::network_params.network };
	const_cast<uint8_t &> (keepalive.header.version_using) = nano::dev::network_params.network.protocol_version_min - 1;
	channel->send (keepalive, nano::transport::traffic_type::test);

	ASSERT_TIMELY (5s, !channel->alive ());
}
