#include <gtest/gtest.h>
#include <mu_coin/mu_coin.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

#include <thread>
#include <condition_variable>

TEST (ledger, empty)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::address address;
    auto balance (ledger.balance (address));
    ASSERT_TRUE (balance.is_zero ());
}

TEST (ledger, genesis_balance)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 500);
    auto balance (ledger.balance (key1.pub));
    ASSERT_EQ (500, balance);
}

TEST (system, system_genesis)
{
    mu_coin::system system (24000, 25000, 2);
    mu_coin::keypair key1;
    system.genesis (key1.pub, 500);
    for (auto & i: system.clients)
    {
        ASSERT_EQ (500, i->ledger.balance (key1.pub));
    }
}

TEST (uint256_union, key_encryption)
{
    mu_coin::keypair key1;
    mu_coin::uint256_union secret_key;
    secret_key.bytes.fill (0);
    mu_coin::uint256_union encrypted (key1.prv, secret_key, key1.pub.owords [0]);
    mu_coin::private_key key4 (encrypted.prv (secret_key, key1.pub.owords [0]));
    ASSERT_EQ (key1.prv, key4);
    mu_coin::public_key pub;
    ed25519_publickey (key4.bytes.data (), pub.bytes.data ());
    ASSERT_EQ (key1.pub, pub);
}

TEST (ledger, process_send)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash block1;
    ASSERT_FALSE (store.latest_get (key1.pub, block1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = block1;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    ASSERT_EQ (50, ledger.balance (key1.pub));
    mu_coin::block_hash hash5;
    ASSERT_FALSE (store.latest_get (key1.pub, hash5));
    auto latest6 (store.block_get (hash5));
    ASSERT_NE (nullptr, latest6);
    auto latest7 (dynamic_cast <mu_coin::send_block *> (latest6.get ()));
    ASSERT_NE (nullptr, latest7);
    ASSERT_EQ (send, *latest7);
    mu_coin::receive_block receive;
    receive.hashables.source = hash1;
    receive.hashables.previous = key2.pub;
    mu_coin::block_hash hash2 (receive.hash ());
    receive.sign (key2.prv, key2.pub, hash2);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (receive));
    ASSERT_EQ (50, ledger.balance (key2.pub));
    mu_coin::block_hash hash3;
    ASSERT_FALSE (store.latest_get (key1.pub, hash3));
    auto latest2 (store.block_get (hash3));
    ASSERT_NE (nullptr, latest2);
    auto latest3 (dynamic_cast <mu_coin::send_block *> (latest2.get ()));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (send, *latest3);
    mu_coin::block_hash hash4;
    ASSERT_FALSE (store.latest_get (key2.pub, hash4));
    auto latest4 (store.block_get (hash4));
    ASSERT_NE (nullptr, latest4);
    auto latest5 (dynamic_cast <mu_coin::receive_block *> (latest4.get ()));
    ASSERT_NE (nullptr, latest5);
    ASSERT_EQ (receive, *latest5);
}

TEST (ledger, process_duplicate)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash block1;
    ASSERT_FALSE (store.latest_get (key1.pub, block1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = block1;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    ASSERT_EQ (mu_coin::process_result::old, ledger.process (send));
    mu_coin::receive_block receive;
    receive.hashables.source = hash1;
    receive.hashables.previous = key2.pub;
    mu_coin::block_hash hash2 (receive.hash ());
    receive.sign (key2.prv, key2.pub, hash2);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (receive));
    ASSERT_EQ (mu_coin::process_result::old, ledger.process (receive));
}

TEST (processor_service, bad_send_signature)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash block1;
    ASSERT_FALSE (store.latest_get (key1.pub, block1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.previous = block1;
    send.hashables.balance = 50;
    send.hashables.destination = key1.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    send.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (mu_coin::process_result::bad_signature, ledger.process (send));
}

TEST (processor_service, bad_receive_signature)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    store.genesis_put (key1.pub, 100);
    mu_coin::block_hash block1;
    ASSERT_FALSE (store.latest_get (key1.pub, block1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.previous = block1;
    send.hashables.balance = 50;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    mu_coin::block_hash hash5;
    ASSERT_FALSE (store.latest_get (key1.pub, hash5));
    mu_coin::receive_block receive;
    receive.hashables.source = hash1;
    receive.hashables.previous = key2.pub;
    mu_coin::block_hash hash2 (receive.hash ());
    receive.sign (key2.prv, key2.pub, hash2);
    receive.signature.bytes [32] ^= 0x1;
    ASSERT_EQ (mu_coin::process_result::bad_signature, ledger.process (receive));
}

TEST (processor_service, empty)
{
    mu_coin::processor_service service;
    std::thread thread ([&service] () {service.run ();});
    service.stop ();
    thread.join ();
}

TEST (processor_service, one)
{
    mu_coin::processor_service service;
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
    mu_coin::processor_service service;
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