#include <gtest/gtest.h>
#include <rai/core/core.hpp>

#include <thread>
#include <atomic>
#include <condition_variable>

TEST (processor_service, bad_send_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::send_block send;
    rai::keypair key2;
    send.hashables.previous = frontier1.hash;
    send.hashables.balance = 50;
    send.hashables.destination = rai::test_genesis_key.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
    send.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (rai::process_result::bad_signature, ledger.process (send));
}

TEST (processor_service, bad_receive_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::send_block send;
    rai::keypair key2;
    send.hashables.previous = frontier1.hash;
    send.hashables.balance = 50;
    send.hashables.destination = key2.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier2));
    rai::receive_block receive;
    receive.hashables.source = hash1;
    receive.hashables.previous = key2.pub;
    rai::sign_message (key2.prv, key2.pub, receive.hash (), receive.signature);
    receive.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (rai::process_result::bad_signature, ledger.process (receive));
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