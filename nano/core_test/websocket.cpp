#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <nano/core_test/testutil.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/testing.hpp>
#include <nano/node/websocket.hpp>
#include <sstream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
std::atomic<bool> ack_ready{ false };
/** A simple blocking websocket client for testing */
std::string websocket_test_call (boost::asio::io_context & ioc, std::string host, std::string port, std::string message_a, bool await_ack)
{
	boost::asio::ip::tcp::resolver resolver{ ioc };
	boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws{ ioc };

	auto const results = resolver.resolve (host, port);
	boost::asio::connect (ws.next_layer (), results.begin (), results.end ());

	ws.handshake (host, "/");
	ws.text (true);
	ws.write (boost::asio::buffer (message_a));

	if (await_ack)
	{
		boost::beast::flat_buffer buffer;
		ws.read (buffer);
		ack_ready = true;
	}

	boost::beast::flat_buffer buffer;
	ws.read (buffer);
	std::ostringstream res;
	res << boost::beast::buffers (buffer.data ());

	ws.close (boost::beast::websocket::close_code::normal);
	return res.str ();
}
}

/** Subscribes to block confirmations, confirms a block and then awaits websocket notification */
TEST (websocket, confirmation)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::node_config config;
	nano::node_flags node_flags;
	config.websocket_config.enabled = true;
	config.websocket_config.port = 24078;

	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, nano::unique_path (), system.alarm, config, system.work, node_flags));
	nano::uint256_union wallet;
	nano::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
	node1->wallets.create (wallet);
	node1->start ();
	system.nodes.push_back (node1);

	// Start websocket test-client in a separate thread
	std::atomic<bool> confirmation_event_received{ false };
	std::thread client_thread ([&system, &confirmation_event_received]() {
		// This will expect two results: the acknowledgement of the subscription
		// and then the block confirmation message
		std::string response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true);

		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response;
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
		confirmation_event_received = true;
	});
	client_thread.detach ();

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Quick-confirm a block
	nano::keypair key;
	nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
	system.wallet (1)->insert_adhoc (key.prv);
	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
	auto send (std::make_shared<nano::send_block> (previous, key.pub, node1->config.online_weight_minimum.number () + 1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
	node1->process_active (send);

	// Wait for the websocket client to receive the confirmation message
	system.deadline_set (5s);
	while (!confirmation_event_received)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}
