#include <gtest/gtest.h>
#include <rai/node.hpp>

#include <thread>
#include <atomic>
#include <condition_variable>

TEST (processor_service, bad_send_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::keypair key2;
    rai::send_block send (rai::test_genesis_key.pub, frontier1.hash, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::block_hash hash1 (send.hash ());
    send.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (rai::process_result::bad_signature, ledger.process (transaction, send).code);
}

TEST (processor_service, bad_receive_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::keypair key2;
    rai::send_block send (key2.pub, frontier1.hash, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::block_hash hash1 (send.hash ());
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send).code);
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier2));
    rai::receive_block receive (key2.pub, hash1, key2.prv, key2.pub, 0);
    receive.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (rai::process_result::bad_signature, ledger.process (transaction, receive).code);
}

TEST (processor_service, empty)
{
    rai::processor_service service;
    std::thread thread ([&service] () {service.run ();});
    service.stop ();
    thread.join ();
}

TEST (processor_service, one)
{
    rai::processor_service service;
    std::atomic <bool> done (false);
    std::mutex mutex;
    std::condition_variable condition;
    service.add (std::chrono::system_clock::now (), [&] ()
                 {
                     std::lock_guard <std::mutex> lock (mutex);
                     done = true;
                     condition.notify_one ();
                 });
    std::thread thread ([&service] () {service.run ();});
    std::unique_lock <std::mutex> unique (mutex);
    condition.wait (unique, [&] () {return !!done;});
    service.stop ();
    thread.join ();
}

TEST (processor_service, many)
{
    rai::processor_service service;
    std::atomic <int> count (0);
    std::mutex mutex;
    std::condition_variable condition;
    for (auto i (0); i < 50; ++i)
    {
        service.add (std::chrono::system_clock::now (), [&] ()
                     {
                         std::lock_guard <std::mutex> lock (mutex);
                         count += 1;
                         condition.notify_one ();
                     });
    }
    std::vector <std::thread> threads;
    for (auto i (0); i < 50; ++i)
    {
        threads.push_back (std::thread ([&service] () {service.run ();}));
    }
    std::unique_lock <std::mutex> unique (mutex);
    condition.wait (unique, [&] () {return count == 50;});
    service.stop ();
    for (auto i (threads.begin ()), j (threads.end ()); i != j; ++i)
    {
        i->join ();
    }
}

TEST (processor_service, top_execution)
{
    rai::processor_service service;
    int value (0);
    std::mutex mutex;
    std::unique_lock <std::mutex> lock1 (mutex);
    service.add (std::chrono::system_clock::now (), [&] () {value = 1; service.stop (); lock1.unlock ();});
    service.add (std::chrono::system_clock::now () + std::chrono::milliseconds (1), [&] () {value = 2; service.stop (); lock1.unlock ();});
    service.run ();
    std::unique_lock <std::mutex> lock2 (mutex);
    ASSERT_EQ (1, value);
}

TEST (processor_service, stopping)
{
    rai::processor_service service;
    ASSERT_EQ (0, service.operations.size ());
    service.add (std::chrono::system_clock::now (), [] () {});
    ASSERT_EQ (1, service.operations.size ());
    service.stop ();
    ASSERT_EQ (0, service.operations.size ());
    service.add (std::chrono::system_clock::now (), [] () {});
    ASSERT_EQ (0, service.operations.size ());
}