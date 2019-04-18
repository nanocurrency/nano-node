#include <boost/asio.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <condition_variable>
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
/** This variable must be set to false before setting up every thread that makes a websocket test call (and needs ack), to be safe */
std::atomic<bool> ack_ready{ false };

/** An optionally blocking websocket client for testing */
boost::optional<std::string> websocket_test_call (boost::asio::io_context & ioc, std::string host, std::string port, std::string message_a, bool await_ack, bool await_response, seconds response_deadline = 5s)
{
	if (await_ack)
	{
		ack_ready = false;
	}

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

	boost::optional<std::string> ret;

	if (await_response)
	{
		boost::asio::deadline_timer timer (ioc);
		std::atomic<bool> timed_out{ false }, got_response{ false };
		std::mutex cond_mutex;
		std::condition_variable cond_var;
		timer.expires_from_now (boost::posix_time::seconds (response_deadline.count ()));
		timer.async_wait ([&ws, &cond_mutex, &cond_var, &timed_out](boost::system::error_code const & ec) {
			if (!ec)
			{
				std::unique_lock<std::mutex> lock (cond_mutex);
				ws.next_layer ().cancel ();
				timed_out = true;
				cond_var.notify_one ();
			}
		});

		boost::beast::flat_buffer buffer;
		ws.async_read (buffer, [&ret, &buffer, &cond_mutex, &cond_var, &got_response](boost::beast::error_code const & ec, std::size_t const n) {
			if (!ec)
			{
				std::unique_lock<std::mutex> lock (cond_mutex);
				std::ostringstream res;
				res << boost::beast::buffers (buffer.data ());
				ret = res.str ();
				got_response = true;
				cond_var.notify_one ();
			}
		});
		std::unique_lock<std::mutex> lock (cond_mutex);
		cond_var.wait (lock, [&] { return timed_out || got_response; });
		if (got_response)
		{
			timer.cancel ();
			ws.close (boost::beast::websocket::close_code::normal);
		}
	}
	return ret;
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
	ack_ready = false;
	std::atomic<bool> confirmation_event_received{ false };
	ASSERT_FALSE (node1->websocket_server->any_subscribers (nano::websocket::topic::confirmation));
	std::thread client_thread ([&system, &confirmation_event_received]() {
		// This will expect two results: the acknowledgement of the subscription
		// and then the block confirmation message
		auto response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
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
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscribers (nano::websocket::topic::confirmation));

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

	std::atomic<bool> unsubscribe_ack_received{ false };
	std::thread client_thread_2 ([&system, &unsubscribe_ack_received]() {
		auto response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": true})json", true, true);
		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");

		// Unsubscribe action, expects an acknowledge but no response follows
		websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "unsubscribe", "topic": "confirmation", "ack": true})json", true, false);
		unsubscribe_ack_received = true;
	});
	client_thread_2.detach ();

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
	ASSERT_FALSE (node1->websocket_server->any_subscribers (nano::websocket::topic::confirmation));
	std::thread client_thread ([&system, &client_thread_finished]() {
		// Subscribe initially with a specific invalid account
		auto response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"accounts": ["xrb_invalid"]}})json", true, true, 1s);

		ASSERT_FALSE (response);
		client_thread_finished = true;
	});
	client_thread.detach ();

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
	std::thread client_thread_2 ([&system, &client_thread_2_finished]() {
		// Re-subscribe with options for all local wallet accounts
		auto response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"all_local_accounts": "true"}})json", true, true);

		ASSERT_TRUE (response);
		boost::property_tree::ptree event;
		std::stringstream stream;
		stream << response.get ();
		boost::property_tree::read_json (stream, event);
		ASSERT_EQ (event.get<std::string> ("topic"), "confirmation");

		client_thread_2_finished = true;
	});
	client_thread_2.detach ();

	// Wait for the subscribe action to be acknowledged
	system.deadline_set (5s);
	while (!ack_ready)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ack_ready = false;

	ASSERT_TRUE (node1->websocket_server->any_subscribers (nano::websocket::topic::confirmation));

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
	std::thread client_thread_3 ([&system, &client_thread_3_finished]() {
		auto response = websocket_test_call (system.io_ctx, "::1", "24078",
		R"json({"action": "subscribe", "topic": "confirmation", "ack": "true", "options": {"all_local_accounts": "true"}})json", true, true, 1s);

		ASSERT_FALSE (response);
		client_thread_3_finished = true;
	});
	client_thread_3.detach ();

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

	node1->stop ();
}
