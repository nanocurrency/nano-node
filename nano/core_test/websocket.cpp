#include <nano/core_test/testutil.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/testing.hpp>
#include <nano/node/websocket.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
/** This variable must be set to false before setting up every thread that makes a websocket test call (and needs ack), to be safe */
std::atomic<bool> ack_ready{ false };

/** An optionally blocking websocket client for testing */
boost::optional<std::string> websocket_test_call (std::string host, std::string port, std::string message_a, bool await_ack, bool await_response, const std::chrono::seconds response_deadline = 5s)
{
	if (await_ack)
	{
		ack_ready = false;
	}

	boost::optional<std::string> ret;
	boost::asio::io_context ioc;
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

	if (await_response)
	{
		assert (response_deadline > 0s);
		auto buffer (std::make_shared<boost::beast::flat_buffer> ());
		ws.async_read (*buffer, [&ret, buffer](boost::beast::error_code const & ec, std::size_t const n) {
			if (!ec)
			{
				std::ostringstream res;
				res << beast_buffers (buffer->data ());
				ret = res.str ();
			}
		});
		ioc.run_one_for (response_deadline);
	}

	if (ws.is_open ())
	{
		boost::beast::error_code ec_ignored;
		ws.async_close (boost::beast::websocket::close_code::normal, [](boost::beast::error_code const & ec) {
			// A synchronous close usually hangs in tests when the server's io_context stops looping
			// An async_close solves this problem
		});
	}
	return ret;
}
}

/** Tests clients subscribing multiple times or unsubscribing without a subscription */
TEST (websocket, subscription_edge)
{
	nano::system system (24000, 1);
	nano::node_init init1;
	nano::node_config config;
	nano::node_flags node_flags;
	config.websocket_config.enabled = true;
	config.websocket_config.port = 24078;

	auto node1 (std::make_shared<nano::node> (init1, system.io_ctx, nano::unique_path (), system.alarm, config, system.work, node_flags));
	node1->start ();
	system.nodes.push_back (node1);

	ASSERT_EQ (0, node1->websocket_server->subscriber_count (nano::websocket::topic::confirmation));

	// First subscription
	{
		ack_ready = false;
		std::thread subscription_thread ([]() {
			websocket_test_call ("::1", "24078", R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, false);
		});
		system.deadline_set (5s);
		while (!ack_ready)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		subscription_thread.join ();
		ASSERT_EQ (1, node1->websocket_server->subscriber_count (nano::websocket::topic::confirmation));
	}

	// Second subscription, should not increase subscriber count, only update the subscription
	{
		ack_ready = false;
		std::thread subscription_thread ([]() {
			websocket_test_call ("::1", "24078", R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, false);
		});
		system.deadline_set (5s);
		while (!ack_ready)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		subscription_thread.join ();
		ASSERT_EQ (1, node1->websocket_server->subscriber_count (nano::websocket::topic::confirmation));
	}

	// First unsub
	{
		ack_ready = false;
		std::thread unsub_thread ([]() {
			websocket_test_call ("::1", "24078", R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, false);
		});
		system.deadline_set (5s);
		while (!ack_ready)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		unsub_thread.join ();
		ASSERT_EQ (0, node1->websocket_server->subscriber_count (nano::websocket::topic::confirmation));
	}

	// Second unsub, should acknowledge but not decrease subscriber count
	{
		ack_ready = false;
		std::thread unsub_thread ([]() {
			websocket_test_call ("::1", "24078", R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, false);
		});
		system.deadline_set (5s);
		while (!ack_ready)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		unsub_thread.join ();
		ASSERT_EQ (0, node1->websocket_server->subscriber_count (nano::websocket::topic::confirmation));
	}

	node1->stop ();
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
	ack_ready = false;
	std::atomic<bool> confirmation_event_received{ false };
	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
	std::thread client_thread ([&confirmation_event_received]() {
		// This will expect two results: the acknowledgement of the subscription
		// and then the block confirmation message
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");
		confirmation_event_received = true;
	});

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));

	nano::keypair key;
	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
	auto balance = nano::genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	// Quick-confirm a block, legacy blocks should work without filtering
	{
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::send_block> (previous, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	}

	// Wait for the confirmation to be received
	system.deadline_set (5s);
	while (!confirmation_event_received)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;
	client_thread.join ();

	std::atomic<bool> unsubscribe_ack_received{ false };
	std::thread client_thread_2 ([&unsubscribe_ack_received]() {
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");

		// Unsubscribe action, expects an acknowledge but no response follows
		websocket_test_call ("::1", "24078",
		R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, true, 1s);
		unsubscribe_ack_received = true;
	});

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	// Quick confirm a state block
	{
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	}

	// Wait for the unsubscribe action to be acknowledged
	system.deadline_set (5s);
	while (!unsubscribe_ack_received)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;
	client_thread_2.join ();

	node1->stop ();
}

/** Tests the filtering options of block confirmations */
TEST (websocket, confirmation_options)
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
	ack_ready = false;
	std::atomic<bool> client_thread_finished{ false };
	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));
	std::thread client_thread ([&client_thread_finished]() {
		// Subscribe initially with a specific invalid account
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"accounts": ["xrb_invalid"]}})json", true, true, 1s);

		ASSERT_FALSE (response);
		client_thread_finished = true;
	});

	// Wait for subscribe acknowledgement
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	// Confirm a state block for an in-wallet account
	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key;
	auto balance = nano::genesis_amount;
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	{
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	}

	// Wait for client thread to finish, no confirmation message should be received with given filter
	system.deadline_set (5s);
	while (!client_thread_finished)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	std::atomic<bool> client_thread_2_finished{ false };
	std::thread client_thread_2 ([&client_thread_2_finished]() {
		// Re-subscribe with options for all local wallet accounts
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"all_local_accounts": "true"}})json", true, true);

		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");

		client_thread_2_finished = true;
	});

	// Wait for the subscribe action to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::confirmation));

	// Quick-confirm another block
	{
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	}

	// Wait for confirmation message
	system.deadline_set (5s);
	while (!client_thread_2_finished)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	std::atomic<bool> client_thread_3_finished{ false };
	std::thread client_thread_3 ([&client_thread_3_finished]() {
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"all_local_accounts": "true"}})json", true, true, 1s);

		ASSERT_FALSE (response);
		client_thread_3_finished = true;
	});

	// Confirm a legacy block
	// When filtering options are enabled, legacy blocks are always filtered
	{
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::send_block> (previous, key.pub, balance, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	}

	// Wait for client thread to finish, no confirmation message should be received
	system.deadline_set (5s);
	while (!client_thread_3_finished)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	client_thread.join ();
	client_thread_2.join ();
	client_thread_3.join ();
	node1->stop ();
}

/** Subscribes to votes, sends a block and awaits websocket notification of a vote arrival */
TEST (websocket, vote)
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
	ack_ready = false;
	std::atomic<bool> client_thread_finished{ false };
	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::vote));
	std::thread client_thread ([&client_thread_finished]() {
		// This will expect two results: the acknowledgement of the subscription
		// and then the vote message
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "vote", "ack": true})json", true, true);

		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response;
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "vote");
		client_thread_finished = true;
	});

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::vote));

	// Quick-confirm a block
	nano::keypair key;
	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
	nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
	auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, nano::genesis_amount - (node1->config.online_weight_minimum.number () + 1), key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
	node1->process_active (send);

	// Wait for the websocket client to receive the vote message
	system.deadline_set (5s);
	while (!client_thread_finished)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	client_thread.join ();
	node1->stop ();
}

/** Tests vote subscription options */
TEST (websocket, vote_options)
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
	ack_ready = false;
	std::atomic<bool> client_thread_finished{ false };
	ASSERT_FALSE (node1->websocket_server->any_subscriber (nano::websocket::topic::vote));
	std::thread client_thread ([&client_thread_finished]() {
		std::ostringstream data;
		data << R"json({"action": "subscribe", "topic": "vote", "ack": true, "options": {"representatives": [")json"
		     << nano::test_genesis_key.pub.to_account ()
		     << R"json("]}})json";
		auto response = websocket_test_call ("::1", "24078", data.str (), true, true);

		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response;
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "vote");
		client_thread_finished = true;
	});

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::vote));

	// Quick-confirm a block
	nano::keypair key;
	auto balance = nano::genesis_amount;
	system.wallet (1)->insert_adhoc (nano::test_genesis_key.prv);
	auto send_amount = node1->config.online_weight_minimum.number () + 1;
	auto confirm_block = [&]() {
		nano::block_hash previous (node1->latest (nano::test_genesis_key.pub));
		balance -= send_amount;
		auto send (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, previous, nano::test_genesis_key.pub, balance, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, system.work.generate (previous)));
		node1->process_active (send);
	};
	confirm_block ();

	// Wait for the websocket client to receive the vote message
	system.deadline_set (5s);
	while (!client_thread_finished || node1->websocket_server->any_subscriber (nano::websocket::topic::vote))
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::atomic<bool> client_thread_2_finished{ false };
	std::thread client_thread_2 ([&client_thread_2_finished]() {
		auto response = websocket_test_call ("::1", "24078",
		R"json({"action": "subscribe", "topic": "vote", "ack": true, "options": {"representatives": ["xrb_invalid"]}})json", true, true, 1s);

		// No response expected given the filter
		ASSERT_FALSE (response);
		client_thread_2_finished = true;
	});

	// Wait for the subscription to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscriber (nano::websocket::topic::vote));

	// Confirm another block
	confirm_block ();

	// No response expected
	system.deadline_set (5s);
	while (!client_thread_2_finished)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	client_thread.join ();
	client_thread_2.join ();
	node1->stop ();
}
