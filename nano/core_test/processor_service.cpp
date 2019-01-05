#include <gtest/gtest.h>
#include <nano/node/node.hpp>

#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>

TEST (processor_service, bad_send_signature)
{
	nano::logging logging;
	bool init (false);
	nano::mdb_store store (init, logging, nano::unique_path ());
	ASSERT_FALSE (init);
	nano::stat stats;
	nano::ledger ledger (store, stats);
	nano::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	nano::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, nano::test_genesis_key.pub, info1));
	nano::keypair key2;
	nano::send_block send (info1.head, nano::test_genesis_key.pub, 50, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0);
	send.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (nano::process_result::bad_signature, ledger.process (transaction, send).code);
}

TEST (processor_service, bad_receive_signature)
{
	nano::logging logging;
	bool init (false);
	nano::mdb_store store (init, logging, nano::unique_path ());
	ASSERT_FALSE (init);
	nano::stat stats;
	nano::ledger ledger (store, stats);
	nano::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	nano::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, nano::test_genesis_key.pub, info1));
	nano::send_block send (info1.head, nano::test_genesis_key.pub, 50, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0);
	nano::block_hash hash1 (send.hash ());
	ASSERT_EQ (nano::process_result::progress, ledger.process (transaction, send).code);
	nano::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, nano::test_genesis_key.pub, info2));
	nano::receive_block receive (hash1, hash1, nano::test_genesis_key.prv, nano::test_genesis_key.pub, 0);
	receive.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (nano::process_result::bad_signature, ledger.process (transaction, receive).code);
}

TEST (alarm, one)
{
	boost::asio::io_context io_ctx;
	nano::alarm alarm (io_ctx);
	std::atomic<bool> done (false);
	std::mutex mutex;
	std::condition_variable condition;
	alarm.add (std::chrono::steady_clock::now (), [&]() {
		{
			std::lock_guard<std::mutex> lock (mutex);
			done = true;
		}
		condition.notify_one ();
	});
	boost::asio::io_context::work work (io_ctx);
	boost::thread thread ([&io_ctx]() { io_ctx.run (); });
	std::unique_lock<std::mutex> unique (mutex);
	condition.wait (unique, [&]() { return !!done; });
	io_ctx.stop ();
	thread.join ();
}

TEST (alarm, many)
{
	boost::asio::io_context io_ctx;
	nano::alarm alarm (io_ctx);
	std::atomic<int> count (0);
	std::mutex mutex;
	std::condition_variable condition;
	for (auto i (0); i < 50; ++i)
	{
		alarm.add (std::chrono::steady_clock::now (), [&]() {
			{
				std::lock_guard<std::mutex> lock (mutex);
				count += 1;
			}
			condition.notify_one ();
		});
	}
	boost::asio::io_context::work work (io_ctx);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 50; ++i)
	{
		threads.push_back (boost::thread ([&io_ctx]() { io_ctx.run (); }));
	}
	std::unique_lock<std::mutex> unique (mutex);
	condition.wait (unique, [&]() { return count == 50; });
	io_ctx.stop ();
	for (auto i (threads.begin ()), j (threads.end ()); i != j; ++i)
	{
		i->join ();
	}
}

TEST (alarm, top_execution)
{
	boost::asio::io_context io_ctx;
	nano::alarm alarm (io_ctx);
	int value1 (0);
	int value2 (0);
	std::mutex mutex;
	std::promise<bool> promise;
	alarm.add (std::chrono::steady_clock::now (), [&]() {
		std::lock_guard<std::mutex> lock (mutex);
		value1 = 1;
		value2 = 1;
	});
	alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (1), [&]() {
		std::lock_guard<std::mutex> lock (mutex);
		value2 = 2;
		promise.set_value (false);
	});
	boost::asio::io_context::work work (io_ctx);
	boost::thread thread ([&io_ctx]() {
		io_ctx.run ();
	});
	promise.get_future ().get ();
	std::lock_guard<std::mutex> lock (mutex);
	ASSERT_EQ (1, value1);
	ASSERT_EQ (2, value2);
	io_ctx.stop ();
	thread.join ();
}
