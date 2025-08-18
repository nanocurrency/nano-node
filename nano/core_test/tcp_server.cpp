#include <nano/node/node.hpp>
#include <nano/node/transport/tcp_server.hpp>
#include <nano/node/transport/tcp_socket.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * Test case for valid handshake to ensure normal operation still works
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

	nano::message_header header (nano::dev::network_params.network, static_cast<nano::message_type> (0xFF)); // Invalid message type
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
	nano::message_header header (nano::dev::network_params.network, nano::message_type::node_id_handshake);

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
	nano::message_header header (nano::dev::network_params.network, nano::message_type::keepalive);
	header.version_using = nano::dev::network_params.network.protocol_version;
	header.version_min = nano::dev::network_params.network.protocol_version_min;
	header.extensions = 0;

	// Serialize the header
	{
		nano::vectorstream stream (invalid_handshake_data);
		header.serialize (stream);
	}

	// Add a valid keepalive message body
	nano::keepalive message (nano::dev::network_params.network);
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