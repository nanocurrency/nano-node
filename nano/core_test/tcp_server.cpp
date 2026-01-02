#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/node.hpp>
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
	ASSERT_TIMELY (10s, node1->network.find_node_id (node2->node_id.pub));
	ASSERT_TIMELY (10s, node2->network.find_node_id (node1->node_id.pub));

	// Verify no handshake aborts occurred
	ASSERT_EQ (node1->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
	ASSERT_EQ (node2->stats.count (nano::stat::type::tcp_server, nano::stat::detail::handshake_abort), 0);
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
	response.v2 = nano::messages::node_id_handshake::response_payload::v2_payload{
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