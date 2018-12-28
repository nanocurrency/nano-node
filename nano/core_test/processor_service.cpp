#include <gtest/gtest.h>
#include <rai/node/node.hpp>

#include <atomic>
#include <condition_variable>
#include <future>
#include <thread>

TEST (processor_service, bad_send_signature)
{
	bool init (false);
	rai::mdb_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::stat stats;
	rai::ledger ledger (store, stats);
	rai::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	rai::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, rai::test_genesis_key.pub, info1));
	rai::keypair key2;
	rai::send_block send (info1.head, rai::test_genesis_key.pub, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	send.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (rai::process_result::bad_signature, ledger.process (transaction, send).code);
}

TEST (processor_service, bad_receive_signature)
{
	bool init (false);
	rai::mdb_store store (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::stat stats;
	rai::ledger ledger (store, stats);
	rai::genesis genesis;
	auto transaction (store.tx_begin (true));
	store.initialize (transaction, genesis);
	rai::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, rai::test_genesis_key.pub, info1));
	rai::send_block send (info1.head, rai::test_genesis_key.pub, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::block_hash hash1 (send.hash ());
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
	rai::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, rai::test_genesis_key.pub, info2));
	rai::receive_block receive (hash1, hash1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	receive.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (rai::process_result::bad_signature, ledger.process (transaction, receive).code);
}

TEST (alarm, one)
{
	boost::asio::io_context io_ctx;
	rai::alarm alarm (io_ctx);
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
	rai::alarm alarm (io_ctx);
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
	rai::alarm alarm (io_ctx);
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
