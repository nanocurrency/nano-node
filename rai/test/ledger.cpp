#include <gtest/gtest.h>
#include <rai/core/core.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

#include <thread>
#include <atomic>
#include <condition_variable>

TEST (ledger, store_error)
{
    leveldb::Status init;
    rai::block_store store (init, boost::filesystem::path {});
    ASSERT_FALSE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_TRUE (init1);
}

TEST (ledger, empty)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::address address;
    auto balance (ledger.account_balance (address));
    ASSERT_TRUE (balance.is_zero ());
}

TEST (ledger, genesis_balance)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    auto balance (ledger.account_balance (rai::genesis_address));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), balance);
    rai::frontier frontier;
    ASSERT_FALSE (store.latest_get (rai::genesis_address, frontier));
    ASSERT_GE (store.now (), frontier.time);
    ASSERT_LT (store.now () - frontier.time, 10);
}

TEST (system, system_genesis)
{
    rai::system system (24000, 2);
    for (auto & i: system.clients)
    {
        ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), i->ledger.account_balance (rai::genesis_address));
    }
}

TEST (uint256_union, key_encryption)
{
    rai::keypair key1;
    rai::uint256_union secret_key;
    secret_key.bytes.fill (0);
    rai::uint256_union encrypted (key1.prv, secret_key, key1.pub.owords [0]);
    rai::private_key key4 (encrypted.prv (secret_key, key1.pub.owords [0]));
    ASSERT_EQ (key1.prv, key4);
    rai::public_key pub;
    ed25519_publickey (key4.bytes.data (), pub.bytes.data ());
    ASSERT_EQ (key1.pub, pub);
}

TEST (ledger, process_send)
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
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier2));
    auto latest6 (store.block_get (frontier2.hash));
    ASSERT_NE (nullptr, latest6);
    auto latest7 (dynamic_cast <rai::send_block *> (latest6.get ()));
    ASSERT_NE (nullptr, latest7);
    ASSERT_EQ (send, *latest7);
    rai::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key2.pub;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.weight (key2.pub));
    rai::frontier frontier3;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier3));
    auto latest2 (store.block_get (frontier3.hash));
    ASSERT_NE (nullptr, latest2);
    auto latest3 (dynamic_cast <rai::send_block *> (latest2.get ()));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (send, *latest3);
    rai::frontier frontier4;
    ASSERT_FALSE (store.latest_get (key2.pub, frontier4));
    auto latest4 (store.block_get (frontier4.hash));
    ASSERT_NE (nullptr, latest4);
    auto latest5 (dynamic_cast <rai::open_block *> (latest4.get ()));
    ASSERT_NE (nullptr, latest5);
    ASSERT_EQ (open, *latest5);
	ledger.rollback (hash2);
	rai::frontier frontier5;
	ASSERT_TRUE (ledger.store.latest_get (key2.pub, frontier5));
    rai::address sender1;
    rai::uint256_union amount1;
    rai::address destination1;
	ASSERT_FALSE (ledger.store.pending_get (hash1, sender1, amount1, destination1));
    ASSERT_EQ (rai::test_genesis_key.pub, sender1);
    ASSERT_EQ (key2.pub, destination1);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, amount1.number ());
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier6;
	ASSERT_FALSE (ledger.store.latest_get (rai::test_genesis_key.pub, frontier6));
	ASSERT_EQ (hash1, frontier6.hash);
	ledger.rollback (frontier6.hash);
    rai::frontier frontier7;
	ASSERT_FALSE (ledger.store.latest_get (rai::test_genesis_key.pub, frontier7));
	ASSERT_EQ (frontier1.hash, frontier7.hash);
    rai::address sender2;
    rai::uint256_union amount2;
    rai::address destination2;
	ASSERT_TRUE (ledger.store.pending_get (hash1, sender2, amount2, destination2));
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (ledger, process_receive)
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
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    rai::keypair key3;
    rai::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key3.pub;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.weight (key3.pub));
	rai::send_block send2;
	send2.hashables.balance = 25;
	send2.hashables.previous = hash1;
	send2.hashables.destination = key2.pub;
    rai::block_hash hash3 (send2.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash3, send2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send2));
	rai::receive_block receive;
	receive.hashables.previous = hash2;
	receive.hashables.source = hash3;
	auto hash4 (receive.hash ());
	rai::sign_message (key2.prv, key2.pub, hash4, receive.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (receive));
	ASSERT_EQ (hash4, ledger.latest (key2.pub));
	ASSERT_EQ (25, ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 25, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 25, ledger.weight (key3.pub));
	ledger.rollback (hash4);
	ASSERT_EQ (25, ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
    rai::address sender1;
    rai::uint256_union amount1;
    rai::address destination1;
	ASSERT_FALSE (ledger.store.pending_get (hash3, sender1, amount1, destination1));
    ASSERT_EQ (rai::test_genesis_key.pub, sender1);
    ASSERT_EQ (25, amount1.number ());
}

TEST (ledger, rollback_receiver)
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
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    rai::keypair key3;
    rai::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key3.pub;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.weight (key3.pub));
	ledger.rollback (hash1);
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
	rai::frontier frontier2;
	ASSERT_TRUE (ledger.store.latest_get (key2.pub, frontier2));
    rai::address sender1;
    rai::uint256_union amount1;
    rai::address destination1;
	ASSERT_TRUE (ledger.store.pending_get (frontier2.hash, sender1, amount1, destination1));
}

TEST (ledger, rollback_representation)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key5;
    rai::change_block change1;
    change1.hashables.previous = genesis.hash ();
    change1.hashables.representative = key5.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, change1.hash (), change1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (change1));
    rai::keypair key3;
    rai::change_block change2;
    change2.hashables.previous = change1.hash ();
    change2.hashables.representative = key3.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, change2.hash (), change2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (change2));
    rai::send_block send1;
    rai::keypair key2;
    send1.hashables.balance = 50;
    send1.hashables.previous = change2.hash ();
    send1.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (send1));
    rai::keypair key4;
    rai::open_block open;
    open.hashables.source = send1.hash ();
    open.hashables.representative = key4.pub;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message (key2.prv, key2.pub, hash2, open.signature);
    rai::sign_message(key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    rai::send_block send2;
    send2.hashables.balance = 1;
    send2.hashables.previous = send1.hash ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send2));
    rai::receive_block receive1;
    receive1.hashables.previous = open.hash ();
    receive1.hashables.source = send2.hash ();
    rai::sign_message (key2.prv, key2.pub, receive1.hash (), receive1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (receive1));
    ASSERT_EQ (1, ledger.weight (key3.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 1, ledger.weight (key4.pub));
    ledger.rollback (receive1.hash ());
    ASSERT_EQ (50, ledger.weight (key3.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 50, ledger.weight (key4.pub));
    ledger.rollback (open.hash ());
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (key3.pub));
    ASSERT_EQ (0, ledger.weight (key4.pub));
    ledger.rollback (change2.hash ());
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (key5.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, process_duplicate)
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
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    rai::block_hash hash1 (send.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash1, send.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    ASSERT_EQ (rai::process_result::old, ledger.process (send));
    rai::open_block open;
    open.hashables.source = hash1;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message(key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    ASSERT_EQ (rai::process_result::old, ledger.process (open));
}

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
    rai::block_hash hash2 (receive.hash ());
    receive.sign (key2.prv, key2.pub, hash2);
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

TEST (ledger, representative_genesis)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    ASSERT_EQ (rai::test_genesis_key.pub, ledger.representative (ledger.latest (rai::test_genesis_key.pub)));
}

TEST (ledger, weight)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (rai::test_genesis_key.pub));
}

TEST (ledger, representative_change)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::keypair key2;
    rai::genesis genesis;
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::change_block block;
    block.hashables.representative = key2.pub;
    block.hashables.previous = frontier1.hash;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block.hash (), block.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block));
    ASSERT_EQ (0, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (key2.pub));
	rai::frontier frontier2;
	ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier2));
	ASSERT_EQ (block.hash (), frontier2.hash);
	ledger.rollback (frontier2.hash);
	rai::frontier frontier3;
	ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier3));
	ASSERT_EQ (frontier1.hash, frontier3.hash);
	ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), ledger.weight (rai::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::keypair key2;
    rai::keypair key3;
    rai::genesis genesis;
    genesis.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::send_block block;
    block.hashables.destination = key2.pub;
    block.hashables.previous = frontier1.hash;
    block.hashables.balance = 100;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block.hash (), block.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block));
    rai::send_block block2;
    block2.hashables.destination = key3.pub;
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    ASSERT_EQ (rai::process_result::fork, ledger.process (block2));
}

TEST (ledger, receive_fork)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::keypair key2;
    rai::keypair key3;
    rai::genesis genesis;
    genesis.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::send_block block;
    block.hashables.destination = key2.pub;
    block.hashables.previous = frontier1.hash;
    block.hashables.balance = 100;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block.hash (), block.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block));
    rai::open_block block2;
    block2.hashables.representative = key2.pub;
    block2.hashables.source = block.hash ();
    rai::sign_message(key2.prv, key2.pub, block2.hash (), block2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block2));
    rai::change_block block3;
    block3.hashables.representative = key3.pub;
    block3.hashables.previous = block2.hash ();
    rai::sign_message (key2.prv, key2.pub, block3.hash (), block3.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block3));
    rai::send_block block4;
    block4.hashables.destination = key2.pub;
    block4.hashables.previous = block.hash ();
    block4.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block4.hash (), block4.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block4));
    rai::receive_block block5;
    block5.hashables.previous = block2.hash ();
    block5.hashables.source = block4.hash ();
    rai::sign_message (key2.prv, key2.pub, block5.hash (), block5.signature);
    ASSERT_EQ (rai::process_result::fork, ledger.process (block5));
}

TEST (ledger, checksum_single)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    rai::genesis genesis;
    genesis.initialize (store);
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    store.checksum_put (0, 0, genesis.hash ());
	ASSERT_EQ (genesis.hash (), ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
    rai::change_block block1;
    block1.hashables.previous = ledger.latest (rai::test_genesis_key.pub);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    rai::checksum check1 (ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
	ASSERT_EQ (genesis.hash (), check1);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block1));
    rai::checksum check2 (ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_EQ (block1.hash (), check2);
}

TEST (ledger, checksum_two)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    rai::genesis genesis;
    genesis.initialize (store);
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    store.checksum_put (0, 0, genesis.hash ());
	rai::keypair key2;
    rai::send_block block1;
    block1.hashables.previous = ledger.latest (rai::test_genesis_key.pub);
	block1.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block1));
	rai::checksum check1 (ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
	rai::open_block block2;
	block2.hashables.source = block1.hash ();
	rai::sign_message (key2.prv, key2.pub, block2.hash (), block2.signature);
	ASSERT_EQ (rai::process_result::progress, ledger.process (block2));
	rai::checksum check2 (ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
	ASSERT_EQ (check1, check2 ^ block2.hash ());
}

TEST (ledger, DISABLED_checksum_range)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::checksum check1 (ledger.checksum (0, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_TRUE (check1.is_zero ());
    rai::block_hash hash1 (42);
    rai::checksum check2 (ledger.checksum (0, 42));
    ASSERT_TRUE (check2.is_zero ());
    rai::checksum check3 (ledger.checksum (42, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_EQ (hash1, check3);
}

TEST (system, generate_send_existing)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    system.generate_send_existing (*system.clients [0]);
    rai::frontier frontier2;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier2));
    ASSERT_NE (frontier1.hash, frontier2.hash);
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) == std::numeric_limits <rai::uint256_t>::max ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) != std::numeric_limits <rai::uint256_t>::max ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (system, generate_send_new)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    auto iterator1 (system.clients [0]->store.latest_begin ());
    ++iterator1;
    ASSERT_EQ (system.clients [0]->store.latest_end (), iterator1);
    system.generate_send_new (*system.clients [0]);
    rai::address new_address;
    auto iterator2 (system.clients [0]->wallet.begin ());
    if (iterator2->first != rai::test_genesis_key.pub)
    {
        new_address = iterator2->first;
    }
    ++iterator2;
    ASSERT_NE (system.clients [0]->wallet.end (), iterator2);
    if (iterator2->first != rai::test_genesis_key.pub)
    {
        new_address = iterator2->first;
    }
    ++iterator2;
    ASSERT_EQ (system.clients [0]->wallet.end (), iterator2);
    while (system.clients [0]->ledger.account_balance (new_address) == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
}

TEST (system, generate_mass_activity)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    size_t count (20);
    system.generate_mass_activity (count, *system.clients [0]);
    size_t accounts (0);
    for (auto i (system.clients [0]->store.latest_begin ()), n (system.clients [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (system, DISABLED_generate_mass_activity_long)
{
    rai::system system (24000, 1);
    system.clients [0]->wallet.insert (rai::test_genesis_key.prv);
    size_t count (10000);
    system.generate_mass_activity (count, *system.clients [0]);
    system.clients [0]->log.dump_cerr ();
    size_t accounts (0);
    for (auto i (system.clients [0]->store.latest_begin ()), n (system.clients [0]->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (ledger, representation)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key2;
    rai::send_block block1;
    block1.hashables.balance = std::numeric_limits <rai::uint256_t>::max () - 100;
    block1.hashables.destination = key2.pub;
    block1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block1));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key3;
    rai::open_block block2;
    block2.hashables.representative = key3.pub;
    block2.hashables.source = block1.hash ();
    rai::sign_message (key2.prv, key2.pub, block2.hash (), block2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block2));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::send_block block3;
    block3.hashables.balance = std::numeric_limits <rai::uint256_t>::max () - 200;
    block3.hashables.destination = key2.pub;
    block3.hashables.previous = block1.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block3.hash (), block3.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block3));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::receive_block block4;
    block4.hashables.previous = block2.hash ();
    block4.hashables.source = block3.hash ();
    rai::sign_message (key2.prv, key2.pub, block4.hash (), block4.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block4));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (200, store.representation_get (key3.pub));
    rai::keypair key4;
    rai::change_block block5;
    block5.hashables.previous = block4.hash ();
    block5.hashables.representative = key4.pub;
    rai::sign_message (key2.prv, key2.pub, block5.hash (), block5.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block5));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    rai::keypair key5;
    rai::send_block block6;
    block6.hashables.balance = 100;
    block6.hashables.destination = key5.pub;
    block6.hashables.previous = block5.hash ();
    rai::sign_message (key2.prv, key2.pub, block6.hash (), block6.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block6));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    rai::keypair key6;
    rai::open_block block7;
    block7.hashables.representative = key6.pub;
    block7.hashables.source = block6.hash ();
    rai::sign_message (key5.prv, key5.pub, block7.hash (), block7.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block7));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    rai::send_block block8;
    block8.hashables.balance.clear ();
    block8.hashables.destination = key5.pub;
    block8.hashables.previous = block6.hash ();
    rai::sign_message (key2.prv, key2.pub, block8.hash (), block8.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block8));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    rai::receive_block block9;
    block9.hashables.previous = block7.hash ();
    block9.hashables.source = block8.hash ();
    rai::sign_message (key5.prv, key5.pub, block9.hash (), block9.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block9));
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (0, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (200, store.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key2;
    rai::send_block send1;
    send1.hashables.balance = 1;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send1));
    rai::open_block open1;
    open1.hashables.representative = key2.pub;
    open1.hashables.source = send1.hash ();
    rai::sign_message (key2.prv, key2.pub, open1.hash (), open1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open1));
    rai::open_block open2;
    open2.hashables.representative = rai::test_genesis_key.pub;
    open2.hashables.source = send1.hash ();
    rai::sign_message (key2.prv, key2.pub, open2.hash (), open2.signature);
    ASSERT_EQ (rai::process_result::overreceive, ledger.process (open2));
}

TEST (ledegr, double_receive)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key2;
    rai::send_block send1;
    send1.hashables.balance = 1;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (send1));
    rai::open_block open1;
    open1.hashables.representative = key2.pub;
    open1.hashables.source = send1.hash ();
    rai::sign_message (key2.prv, key2.pub, open1.hash (), open1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open1));
    rai::receive_block receive1;
    receive1.hashables.previous = open1.hash ();
    receive1.hashables.source = send1.hash ();
    rai::sign_message (key2.prv, key2.pub, receive1.hash (), receive1.signature);
    ASSERT_EQ (rai::process_result::overreceive, ledger.process (receive1));
}

TEST (votes, add_unsigned)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    auto votes1 (client1.conflicts.roots.find (client1.store.root (send1))->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.address = key1.pub;
    votes1->vote (vote1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
}

TEST (votes, add_one)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    auto votes1 (client1.conflicts.roots.find (client1.store.root (send1))->second);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.address = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    auto existing1 (votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_NE (votes1->votes.rep_votes.end (), existing1);
    ASSERT_EQ (send1, *existing1->second.second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send1, *winner.first);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), winner.second);
}

TEST (votes, add_two)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    auto votes1 (client1.conflicts.roots.find (client1.store.root (send1))->second);
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.address = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.address = key2.pub;
    vote2.sequence = 1;
    vote2.block = send2.clone ();
    rai::sign_message (key2.prv, key2.pub, vote2.hash (), vote2.signature);
    votes1->vote (vote2);
    ASSERT_EQ (3, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send1, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (key2.pub));
    ASSERT_EQ (send2, *votes1->votes.rep_votes [key2.pub].second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send1, *winner.first);
}

TEST (votes, add_existing)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    auto votes1 (client1.conflicts.roots.find (client1.store.root (send1))->second);
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.address = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.address = rai::test_genesis_key.pub;
    vote2.sequence = 2;
    vote2.block = send2.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote2.hash (), vote2.signature);
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send2, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send2, *winner.first);
}

TEST (votes, add_old)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
	rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    auto votes1 (client1.conflicts.roots.find (client1.store.root (send1))->second);
    rai::vote vote1;
    vote1.sequence = 2;
    vote1.block = send1.clone ();
    vote1.address = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.address = rai::test_genesis_key.pub;
    vote2.sequence = 1;
    vote2.block = send2.clone ();
    rai::sign_message (key2.prv, key2.pub, vote2.hash (), vote2.signature);
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send1, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send1, *winner.first);
}

TEST (conflicts, start_stop)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
	rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    client1.conflicts.start (send1, false);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    auto root1 (client1.store.root (send1));
    auto existing1 (client1.conflicts.roots.find (root1));
    ASSERT_NE (client1.conflicts.roots.end (), existing1);
    auto votes1 (existing1->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    client1.conflicts.stop (root1);
    ASSERT_EQ (0, client1.conflicts.roots.size ());
}

TEST (conflicts, add_existing)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
	rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    client1.conflicts.start (send2, false);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    rai::vote vote1;
    vote1.address = key2.pub;
    vote1.sequence = 0;
    vote1.block = send2.clone ();
    rai::sign_message (key2.prv, key2.pub, vote1.hash (), vote1.signature);
    client1.conflicts.update (vote1);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    auto votes1 (client1.conflicts.roots [client1.store.root (send2)]);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
	rai::genesis genesis;
    rai::send_block send1;
    rai::keypair key1;
    send1.hashables.previous = genesis.hash ();
    send1.hashables.balance.clear ();
    send1.hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send1));
    client1.conflicts.start (send1, false);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = send1.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (send2));
    client1.conflicts.start (send2, false);
    ASSERT_EQ (2, client1.conflicts.roots.size ());
}

TEST (ledger, successor)
{
    rai::system system (24000, 1);
	rai::keypair key1;
	rai::genesis genesis;
	rai::send_block send1;
	send1.hashables.previous = genesis.hash ();
	send1.hashables.balance.clear ();
	send1.hashables.destination = key1.pub;
	rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1.hash (), send1.signature);
	ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (send1));
	ASSERT_EQ (send1, *system.clients [0]->ledger.successor (genesis.hash ()));
}

TEST (fork, publish)
{
    std::weak_ptr <rai::client> client0;
    {
        rai::system system (24000, 1);
        client0 = system.clients [0];
        auto & client1 (*system.clients [0]);
        client1.wallet.insert (rai::test_genesis_key.prv);
        rai::keypair key1;
		rai::genesis genesis;
        std::unique_ptr <rai::send_block> send1 (new rai::send_block);
        send1->hashables.previous = genesis.hash ();
        send1->hashables.balance.clear ();
        send1->hashables.destination = key1.pub;
        rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
        rai::publish_req publish1;
        publish1.block = std::move (send1);
        rai::keypair key2;
        std::unique_ptr <rai::send_block> send2 (new rai::send_block);
        send2->hashables.previous = genesis.hash ();
        send2->hashables.balance.clear ();
        send2->hashables.destination = key2.pub;
        rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
        rai::publish_req publish2;
        publish2.block = std::move (send2);
        client1.processor.process_message (publish1, rai::endpoint {}, true);
        ASSERT_EQ (0, client1.conflicts.roots.size ());
        client1.processor.process_message (publish2, rai::endpoint {}, true);
        ASSERT_EQ (1, client1.conflicts.roots.size ());
        auto conflict1 (client1.conflicts.roots.find (client1.store.root (*publish1.block)));
        ASSERT_NE (client1.conflicts.roots.end (), conflict1);
        auto votes1 (conflict1->second);
        ASSERT_NE (nullptr, votes1);
        ASSERT_EQ (1, votes1->votes.rep_votes.size ());
        while (votes1->votes.rep_votes.size () == 1)
        {
            system.service->poll_one ();
            system.processor.poll_one ();
        }
        ASSERT_EQ (2, votes1->votes.rep_votes.size ());
        auto existing1 (votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
        ASSERT_NE (votes1->votes.rep_votes.end (), existing1);
        ASSERT_EQ (*publish1.block, *existing1->second.second);
        auto winner (votes1->votes.winner ());
        ASSERT_EQ (*publish1.block, *winner.first);
        ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), winner.second);
    }
    ASSERT_TRUE (client0.expired ());
}

TEST (fork, keep)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
	ASSERT_EQ (1, client1.peers.size ());
	client1.wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish_req publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish_req publish2;
    publish2.block = std::move (send2);
    client1.processor.process_message (publish1, rai::endpoint {}, true);
	client2.processor.process_message (publish1, rai::endpoint {}, true);
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, rai::endpoint {}, true);
	client2.processor.process_message (publish2, rai::endpoint {}, true);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    ASSERT_EQ (1, client2.conflicts.roots.size ());
    auto conflict (client2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (client2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	ASSERT_TRUE (system.clients [0]->store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (system.clients [1]->store.block_exists (publish1.block->hash ()));
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
	}
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.first);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), winner.second);
	ASSERT_TRUE (system.clients [0]->store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (system.clients [1]->store.block_exists (publish1.block->hash ()));
}

TEST (fork, flip)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
    ASSERT_EQ (1, client1.peers.size ());
    client1.wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish_req publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish_req publish2;
    publish2.block = std::move (send2);
    client1.processor.process_message (publish1, rai::endpoint {}, true);
    client2.processor.process_message (publish2, rai::endpoint {}, true);
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, rai::endpoint {}, true);
    client2.processor.process_message (publish1, rai::endpoint {}, true);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    ASSERT_EQ (1, client2.conflicts.roots.size ());
    auto conflict (client2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (client2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
    ASSERT_TRUE (client2.store.block_exists (publish2.block->hash ()));
    while (votes1->votes.rep_votes.size () == 1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.first);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), winner.second);
    ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
    ASSERT_TRUE (client2.store.block_exists (publish1.block->hash ()));
    ASSERT_FALSE (client2.store.block_exists (publish2.block->hash ()));
}

TEST (fork, multi_flip)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
	ASSERT_EQ (1, client1.peers.size ());
	client1.wallet.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish_req publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish_req publish2;
    publish2.block = std::move (send2);
    std::unique_ptr <rai::send_block> send3 (new rai::send_block);
    send3->hashables.previous = publish2.block->hash ();
    send3->hashables.balance.clear ();
    send3->hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send3->hash (), send3->signature);
    rai::publish_req publish3;
    publish3.block = std::move (send3);
    client1.processor.process_message (publish1, rai::endpoint {}, true);
	client2.processor.process_message (publish2, rai::endpoint {}, true);
    client2.processor.process_message (publish3, rai::endpoint {}, true);
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, rai::endpoint {}, true);
    client1.processor.process_message (publish3, rai::endpoint {}, true);
	client2.processor.process_message (publish1, rai::endpoint {}, true);
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    ASSERT_EQ (1, client2.conflicts.roots.size ());
    auto conflict (client2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (client2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (client2.store.block_exists (publish2.block->hash ()));
    ASSERT_TRUE (client2.store.block_exists (publish3.block->hash ()));
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
	}
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.first);
    ASSERT_EQ (std::numeric_limits <rai::uint256_t>::max (), winner.second);
	ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (client2.store.block_exists (publish1.block->hash ()));
	ASSERT_FALSE (client2.store.block_exists (publish2.block->hash ()));
    ASSERT_FALSE (client2.store.block_exists (publish3.block->hash ()));
}