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
    auto balance (ledger.account_balance (address));
    ASSERT_TRUE (balance.is_zero ());
}

TEST (ledger, genesis_balance)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 500);
    genesis.initialize (store);
    auto balance (ledger.account_balance (key1.pub));
    ASSERT_EQ (500, balance);
    mu_coin::frontier frontier;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier));
    ASSERT_GE (store.now (), frontier.time);
    ASSERT_LT (store.now () - frontier.time, 10);
}

TEST (system, system_genesis)
{
    mu_coin::system system (1, 24000, 25000, 2, 500);
    for (auto & i: system.clients)
    {
        ASSERT_EQ (500, i->client_m->ledger.account_balance (system.test_genesis_address.pub));
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
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    ASSERT_EQ (50, ledger.account_balance (key1.pub));
    mu_coin::frontier frontier2;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier2));
    auto latest6 (store.block_get (frontier2.hash));
    ASSERT_NE (nullptr, latest6);
    auto latest7 (dynamic_cast <mu_coin::send_block *> (latest6.get ()));
    ASSERT_NE (nullptr, latest7);
    ASSERT_EQ (send, *latest7);
    mu_coin::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key2.pub;
    mu_coin::block_hash hash2 (open.hash ());
    mu_coin::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open));
    ASSERT_EQ (50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (key1.pub));
    ASSERT_EQ (50, ledger.weight (key2.pub));
    mu_coin::frontier frontier3;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier3));
    auto latest2 (store.block_get (frontier3.hash));
    ASSERT_NE (nullptr, latest2);
    auto latest3 (dynamic_cast <mu_coin::send_block *> (latest2.get ()));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (send, *latest3);
    mu_coin::frontier frontier4;
    ASSERT_FALSE (store.latest_get (key2.pub, frontier4));
    auto latest4 (store.block_get (frontier4.hash));
    ASSERT_NE (nullptr, latest4);
    auto latest5 (dynamic_cast <mu_coin::open_block *> (latest4.get ()));
    ASSERT_NE (nullptr, latest5);
    ASSERT_EQ (open, *latest5);
	ledger.rollback (hash2);
	mu_coin::frontier frontier5;
	ASSERT_TRUE (ledger.store.latest_get (key2.pub, frontier5));
    mu_coin::address sender1;
    mu_coin::uint256_union amount1;
    mu_coin::address destination1;
	ASSERT_FALSE (ledger.store.pending_get (hash1, sender1, amount1, destination1));
    ASSERT_EQ (key1.pub, sender1);
    ASSERT_EQ (key2.pub, destination1);
    ASSERT_EQ (50, amount1.number ());
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (key1.pub));
    ASSERT_EQ (100, ledger.weight (key1.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    mu_coin::frontier frontier6;
	ASSERT_FALSE (ledger.store.latest_get (key1.pub, frontier6));
	ASSERT_EQ (hash1, frontier6.hash);
	ledger.rollback (frontier6.hash);
    mu_coin::frontier frontier7;
	ASSERT_FALSE (ledger.store.latest_get (key1.pub, frontier7));
	ASSERT_EQ (frontier1.hash, frontier7.hash);
    mu_coin::address sender2;
    mu_coin::uint256_union amount2;
    mu_coin::address destination2;
	ASSERT_TRUE (ledger.store.pending_get (hash1, sender2, amount2, destination2));
	ASSERT_EQ (100, ledger.account_balance (key1.pub));
}

TEST (ledger, process_receive)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
	ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    mu_coin::keypair key3;
    mu_coin::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key3.pub;
    mu_coin::block_hash hash2 (open.hash ());
    mu_coin::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open));
    ASSERT_EQ (50, ledger.weight (key3.pub));
	mu_coin::send_block send2;
	send2.hashables.balance = 25;
	send2.hashables.previous = hash1;
	send2.hashables.destination = key2.pub;
    mu_coin::block_hash hash3 (send2.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash3, send2.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send2));
	mu_coin::receive_block receive;
	receive.hashables.previous = hash2;
	receive.hashables.source = hash3;
	auto hash4 (receive.hash ());
	mu_coin::sign_message (key2.prv, key2.pub, hash4, receive.signature);
	ASSERT_EQ (mu_coin::process_result::progress, ledger.process (receive));
	ASSERT_EQ (hash4, ledger.latest (key2.pub));
	ASSERT_EQ (25, ledger.account_balance (key1.pub));
	ASSERT_EQ (75, ledger.account_balance (key2.pub));
    ASSERT_EQ (75, ledger.weight (key3.pub));
	ledger.rollback (hash4);
	ASSERT_EQ (25, ledger.account_balance (key1.pub));
	ASSERT_EQ (50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
    mu_coin::address sender1;
    mu_coin::uint256_union amount1;
    mu_coin::address destination1;
	ASSERT_FALSE (ledger.store.pending_get (hash3, sender1, amount1, destination1));
    ASSERT_EQ (key1.pub, sender1);
    ASSERT_EQ (25, amount1.number ());
}

TEST (ledger, rollback_receiver)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
	ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    mu_coin::keypair key3;
    mu_coin::open_block open;
    open.hashables.source = hash1;
    open.hashables.representative = key3.pub;
    mu_coin::block_hash hash2 (open.hash ());
    mu_coin::sign_message (key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (key1.pub));
	ASSERT_EQ (50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (key1.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (50, ledger.weight (key3.pub));
	ledger.rollback (hash1);
	ASSERT_EQ (100, ledger.account_balance (key1.pub));
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
    ASSERT_EQ (100, ledger.weight (key1.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
	mu_coin::frontier frontier2;
	ASSERT_TRUE (ledger.store.latest_get (key2.pub, frontier2));
    mu_coin::address sender1;
    mu_coin::uint256_union amount1;
    mu_coin::address destination1;
	ASSERT_TRUE (ledger.store.pending_get (frontier2.hash, sender1, amount1, destination1));
}

TEST (ledger, rollback_representation)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::keypair key5;
    mu_coin::change_block change1;
    change1.hashables.previous = genesis.hash ();
    change1.hashables.representative = key5.pub;
    mu_coin::sign_message (key1.prv, key1.pub, change1.hash (), change1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (change1));
    mu_coin::keypair key3;
    mu_coin::change_block change2;
    change2.hashables.previous = change1.hash ();
    change2.hashables.representative = key3.pub;
    mu_coin::sign_message (key1.prv, key1.pub, change2.hash (), change2.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (change2));
    mu_coin::send_block send1;
    mu_coin::keypair key2;
    send1.hashables.balance = 50;
    send1.hashables.previous = change2.hash ();
    send1.hashables.destination = key2.pub;
    mu_coin::sign_message (key1.prv, key1.pub, send1.hash (), send1.signature);
	ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send1));
    mu_coin::keypair key4;
    mu_coin::open_block open;
    open.hashables.source = send1.hash ();
    open.hashables.representative = key4.pub;
    mu_coin::block_hash hash2 (open.hash ());
    mu_coin::sign_message (key2.prv, key2.pub, hash2, open.signature);
    mu_coin::sign_message(key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open));
    mu_coin::send_block send2;
    send2.hashables.balance = 1;
    send2.hashables.previous = send1.hash ();
    send2.hashables.destination = key2.pub;
    mu_coin::sign_message (key1.prv, key1.pub, send2.hash (), send2.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send2));
    mu_coin::receive_block receive1;
    receive1.hashables.previous = open.hash ();
    receive1.hashables.source = send2.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, receive1.hash (), receive1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (receive1));
    ASSERT_EQ (1, ledger.weight (key3.pub));
    ASSERT_EQ (99, ledger.weight (key4.pub));
    ledger.rollback (receive1.hash ());
    ASSERT_EQ (50, ledger.weight (key3.pub));
    ASSERT_EQ (50, ledger.weight (key4.pub));
    ledger.rollback (open.hash ());
    ASSERT_EQ (100, ledger.weight (key3.pub));
    ASSERT_EQ (0, ledger.weight (key4.pub));
    ledger.rollback (change2.hash ());
    ASSERT_EQ (100, ledger.weight (key5.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, process_duplicate)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.balance = 50;
    send.hashables.previous = frontier1.hash;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    ASSERT_EQ (mu_coin::process_result::old, ledger.process (send));
    mu_coin::open_block open;
    open.hashables.source = hash1;
    mu_coin::block_hash hash2 (open.hash ());
    mu_coin::sign_message(key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open));
    ASSERT_EQ (mu_coin::process_result::old, ledger.process (open));
}

TEST (processor_service, bad_send_signature)
{
    mu_coin::keypair key1;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.previous = frontier1.hash;
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
    mu_coin::genesis genesis (key1.pub, 100);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block send;
    mu_coin::keypair key2;
    send.hashables.previous = frontier1.hash;
    send.hashables.balance = 50;
    send.hashables.destination = key2.pub;
    mu_coin::block_hash hash1 (send.hash ());
    mu_coin::sign_message (key1.prv, key1.pub, hash1, send.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send));
    mu_coin::frontier frontier2;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier2));
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

TEST (processor_service, top_execution)
{
    mu_coin::processor_service service;
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
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::address address;
    mu_coin::genesis genesis (address);
    genesis.initialize (store);
    ASSERT_EQ (address, ledger.representative (ledger.latest (address)));
}

TEST (ledger, weight)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::address address;
    mu_coin::genesis genesis (address);
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), ledger.weight (address));
}

TEST (ledger, representative_change)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), ledger.weight (key1.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::change_block block;
    block.hashables.representative = key2.pub;
    block.hashables.previous = frontier1.hash;
    mu_coin::sign_message (key1.prv, key1.pub, block.hash (), block.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block));
    ASSERT_EQ (0, ledger.weight (key1.pub));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), ledger.weight (key2.pub));
	mu_coin::frontier frontier2;
	ASSERT_FALSE (store.latest_get (key1.pub, frontier2));
	ASSERT_EQ (block.hash (), frontier2.hash);
	ledger.rollback (frontier2.hash);
	mu_coin::frontier frontier3;
	ASSERT_FALSE (store.latest_get (key1.pub, frontier3));
	ASSERT_EQ (frontier1.hash, frontier3.hash);
	ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), ledger.weight (key1.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::keypair key3;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block block;
    block.hashables.destination = key2.pub;
    block.hashables.previous = frontier1.hash;
    block.hashables.balance = 100;
    mu_coin::sign_message (key1.prv, key1.pub, block.hash (), block.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block));
    mu_coin::send_block block2;
    block2.hashables.destination = key3.pub;
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance.clear ();
    mu_coin::sign_message (key1.prv, key1.pub, block2.hash (), block2.signature);
    ASSERT_EQ (mu_coin::process_result::fork, ledger.process (block2));
}

TEST (ledger, receive_fork)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    mu_coin::keypair key3;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    mu_coin::send_block block;
    block.hashables.destination = key2.pub;
    block.hashables.previous = frontier1.hash;
    block.hashables.balance = 100;
    mu_coin::sign_message (key1.prv, key1.pub, block.hash (), block.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block));
    mu_coin::open_block block2;
    block2.hashables.representative = key2.pub;
    block2.hashables.source = block.hash ();
    mu_coin::sign_message(key2.prv, key2.pub, block2.hash (), block2.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block2));
    mu_coin::change_block block3;
    block3.hashables.representative = key3.pub;
    block3.hashables.previous = block2.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block3.hash (), block3.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block3));
    mu_coin::send_block block4;
    block4.hashables.destination = key2.pub;
    block4.hashables.previous = block.hash ();
    block4.hashables.balance.clear ();
    mu_coin::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block4));
    mu_coin::receive_block block5;
    block5.hashables.previous = block2.hash ();
    block5.hashables.source = block4.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block5.hash (), block5.signature);
    ASSERT_EQ (mu_coin::process_result::fork, ledger.process (block5));
}

TEST (ledger, checksum_single)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::ledger ledger (store);
    store.checksum_put (0, 0, genesis.hash ());
	ASSERT_EQ (genesis.hash (), ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
    mu_coin::change_block block1;
    block1.hashables.previous = ledger.latest (key1.pub);
    mu_coin::sign_message (key1.prv, key1.pub, block1.hash (), block1.signature);
    mu_coin::checksum check1 (ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
	ASSERT_EQ (genesis.hash (), check1);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block1));
    mu_coin::checksum check2 (ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
    ASSERT_EQ (block1.hash (), check2);
}

TEST (ledger, checksum_two)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::keypair key1;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::ledger ledger (store);
    store.checksum_put (0, 0, genesis.hash ());
	mu_coin::keypair key2;
    mu_coin::send_block block1;
    block1.hashables.previous = ledger.latest (key1.pub);
	block1.hashables.destination = key2.pub;
    mu_coin::sign_message (key1.prv, key1.pub, block1.hash (), block1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block1));
	mu_coin::checksum check1 (ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
	mu_coin::open_block block2;
	block2.hashables.source = block1.hash ();
	mu_coin::sign_message (key2.prv, key2.pub, block2.hash (), block2.signature);
	ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block2));
	mu_coin::checksum check2 (ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
	ASSERT_EQ (check1, check2 ^ block2.hash ());
}

TEST (ledger, DISABLED_checksum_range)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::checksum check1 (ledger.checksum (0, std::numeric_limits <mu_coin::uint256_t>::max ()));
    ASSERT_TRUE (check1.is_zero ());
    mu_coin::block_hash hash1 (42);
    mu_coin::checksum check2 (ledger.checksum (0, 42));
    ASSERT_TRUE (check2.is_zero ());
    mu_coin::checksum check3 (ledger.checksum (42, std::numeric_limits <mu_coin::uint256_t>::max ()));
    ASSERT_EQ (hash1, check3);
}

TEST (client, balance)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), system.clients [0]->client_m->balance ());
}

TEST (system, generate_send_existing)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    mu_coin::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->client_m->store.latest_get (system.test_genesis_address.pub, frontier1));
    system.generate_send_existing (*system.clients [0]->client_m);
    mu_coin::frontier frontier2;
    ASSERT_FALSE (system.clients [0]->client_m->store.latest_get (system.test_genesis_address.pub, frontier2));
    ASSERT_NE (frontier1.hash, frontier2.hash);
    system.processor.poll_one ();
    ASSERT_EQ (system.clients [0]->client_m->ledger.account_balance (system.test_genesis_address.pub), std::numeric_limits <mu_coin::uint256_t>::max ());
}

TEST (system, generate_send_new)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    auto iterator1 (system.clients [0]->client_m->store.latest_begin ());
    ++iterator1;
    ASSERT_EQ (system.clients [0]->client_m->store.latest_end (), iterator1);
    system.generate_send_new (*system.clients [0]->client_m);
    system.processor.poll_one ();
    mu_coin::address new_address;
    auto iterator2 (system.clients [0]->client_m->store.latest_begin ());
    if (iterator2->first != system.test_genesis_address.pub)
    {
        new_address = iterator2->first;
    }
    ++iterator2;
    ASSERT_NE (system.clients [0]->client_m->store.latest_end (), iterator2);
    if (iterator2->first != system.test_genesis_address.pub)
    {
        new_address = iterator2->first;
    }
    ++iterator2;
    ASSERT_EQ (system.clients [0]->client_m->store.latest_end (), iterator2);
    ASSERT_LT (system.clients [0]->client_m->ledger.account_balance (system.test_genesis_address.pub), std::numeric_limits <mu_coin::uint256_t>::max ());
    ASSERT_GT (system.clients [0]->client_m->ledger.account_balance (system.test_genesis_address.pub), 0);
}

TEST (system, generate_mass_activity)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    size_t count (20);
    system.generate_mass_activity (count, *system.clients [0]->client_m);
    size_t accounts (0);
    for (auto i (system.clients [0]->client_m->store.latest_begin ()), n (system.clients [0]->client_m->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (system, DISABLED_generate_mass_activity_long)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
    system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    size_t count (10000);
    system.generate_mass_activity (count, *system.clients [0]->client_m);
    system.clients [0]->client_m->log.dump_cerr ();
    size_t accounts (0);
    for (auto i (system.clients [0]->client_m->store.latest_begin ()), n (system.clients [0]->client_m->store.latest_end ()); i != n; ++i)
    {
        ++accounts;
    }
    ASSERT_GT (accounts, count / 10);
}

TEST (ledger, representation)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), store.representation_get (key1.pub));
    mu_coin::keypair key2;
    mu_coin::send_block block1;
    block1.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 100;
    block1.hashables.destination = key2.pub;
    block1.hashables.previous = genesis.hash ();
    mu_coin::sign_message (key1.prv, key1.pub, block1.hash (), block1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block1));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), store.representation_get (key1.pub));
    mu_coin::keypair key3;
    mu_coin::open_block block2;
    block2.hashables.representative = key3.pub;
    block2.hashables.source = block1.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block2.hash (), block2.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block2));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 100, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    mu_coin::send_block block3;
    block3.hashables.balance = std::numeric_limits <mu_coin::uint256_t>::max () - 200;
    block3.hashables.destination = key2.pub;
    block3.hashables.previous = block1.hash ();
    mu_coin::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block3));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 100, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    mu_coin::receive_block block4;
    block4.hashables.previous = block2.hash ();
    block4.hashables.source = block3.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block4.hash (), block4.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block4));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (200, store.representation_get (key3.pub));
    mu_coin::keypair key4;
    mu_coin::change_block block5;
    block5.hashables.previous = block4.hash ();
    block5.hashables.representative = key4.pub;
    mu_coin::sign_message (key2.prv, key2.pub, block5.hash (), block5.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block5));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    mu_coin::keypair key5;
    mu_coin::send_block block6;
    block6.hashables.balance = 100;
    block6.hashables.destination = key5.pub;
    block6.hashables.previous = block5.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block6.hash (), block6.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block6));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    mu_coin::keypair key6;
    mu_coin::open_block block7;
    block7.hashables.representative = key6.pub;
    block7.hashables.source = block6.hash ();
    mu_coin::sign_message (key5.prv, key5.pub, block7.hash (), block7.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block7));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    mu_coin::send_block block8;
    block8.hashables.balance.clear ();
    block8.hashables.destination = key5.pub;
    block8.hashables.previous = block6.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, block8.hash (), block8.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block8));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    mu_coin::receive_block block9;
    block9.hashables.previous = block7.hash ();
    block9.hashables.source = block8.hash ();
    mu_coin::sign_message (key5.prv, key5.pub, block9.hash (), block9.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (block9));
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max () - 200, store.representation_get (key1.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (0, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (200, store.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::keypair key2;
    mu_coin::send_block send1;
    send1.hashables.balance = 1;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    mu_coin::sign_message(key1.prv, key1.pub, send1.hash (), send1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send1));
    mu_coin::open_block open1;
    open1.hashables.representative = key2.pub;
    open1.hashables.source = send1.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, open1.hash (), open1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open1));
    mu_coin::open_block open2;
    open2.hashables.representative = key1.pub;
    open2.hashables.source = send1.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, open2.hash (), open2.signature);
    ASSERT_EQ (mu_coin::process_result::overreceive, ledger.process (open2));
}

TEST (ledegr, double_receive)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::keypair key1;
    mu_coin::genesis genesis (key1.pub);
    genesis.initialize (store);
    mu_coin::keypair key2;
    mu_coin::send_block send1;
    send1.hashables.balance = 1;
    send1.hashables.destination = key2.pub;
    send1.hashables.previous = genesis.hash ();
    mu_coin::sign_message(key1.prv, key1.pub, send1.hash (), send1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (send1));
    mu_coin::open_block open1;
    open1.hashables.representative = key2.pub;
    open1.hashables.source = send1.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, open1.hash (), open1.signature);
    ASSERT_EQ (mu_coin::process_result::progress, ledger.process (open1));
    mu_coin::receive_block receive1;
    receive1.hashables.previous = open1.hash ();
    receive1.hashables.source = send1.hash ();
    mu_coin::sign_message (key2.prv, key2.pub, receive1.hash (), receive1.signature);
    ASSERT_EQ (mu_coin::process_result::overreceive, ledger.process (receive1));
}

TEST (votes, add_unsigned)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::block_hash block (2);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.sequence = 1;
    vote1.block = block;
    vote1.address = 1;
    votes.add (vote1);
    ASSERT_EQ (0, votes.rep_votes.size ());
}

TEST (votes, add_one)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::block_hash block (2);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.sequence = 1;
    vote1.block = block;
    vote1.address = key1.pub;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    votes.add (vote1);
    ASSERT_EQ (1, votes.rep_votes.size ());
    ASSERT_NE (votes.rep_votes.end (), votes.rep_votes.find (key1.pub));
    ASSERT_EQ (block, votes.rep_votes [key1.pub].second);
    auto winner (votes.winner ());
    ASSERT_EQ (block, winner.first);
    ASSERT_EQ (ledger.weight (key1.pub), winner.second);
}

TEST (votes, add_two)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key2;
	mu_coin::genesis genesis (key2.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::keypair key1;
    mu_coin::block_hash block1 (2);
    mu_coin::block_hash block2 (4);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    votes.add (vote1);
    mu_coin::vote vote2;
    vote2.address = key2.pub;
    vote2.sequence = 1;
    vote2.block = block2;
    mu_coin::sign_message (key2.prv, key2.pub, vote2.hash (), vote2.signature);
    votes.add (vote2);
    ASSERT_EQ (2, votes.rep_votes.size ());
    ASSERT_NE (votes.rep_votes.end (), votes.rep_votes.find (key1.pub));
    ASSERT_EQ (block1, votes.rep_votes [key1.pub].second);
    ASSERT_NE (votes.rep_votes.end (), votes.rep_votes.find (key2.pub));
    ASSERT_EQ (block2, votes.rep_votes [key2.pub].second);
    auto winner (votes.winner ());
    ASSERT_EQ (block2, winner.first);
}

TEST (votes, add_existing)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::block_hash block1 (2);
    mu_coin::block_hash block2 (3);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    votes.add (vote1);
    auto winner1 (votes.winner ());
    ASSERT_EQ (block1, winner1.first);
    mu_coin::vote vote2;
    vote2.address = key1.pub;
    vote2.sequence = 2;
    vote2.block = block2;
    mu_coin::sign_message (key1.prv, key1.pub, vote2.hash (), vote2.signature);
    votes.add (vote2);
    auto winner2 (votes.winner ());
    ASSERT_EQ (block2, winner2.first);
    ASSERT_EQ (1, votes.rep_votes.size ());
    ASSERT_NE (votes.rep_votes.end (), votes.rep_votes.find (key1.pub));
    ASSERT_EQ (block2, votes.rep_votes [key1.pub].second);
}

TEST (votes, add_contesting)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key2;
	mu_coin::genesis genesis (key2.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::keypair key1;
    mu_coin::keypair key3;
    mu_coin::block_hash block1 (2);
    mu_coin::block_hash block2 (4);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    votes.add (vote1);
    auto uncontested1 (votes.uncontested ());
    ASSERT_EQ (0, uncontested1);
    auto winner1 (votes.winner ());
    ASSERT_EQ (block1, winner1.first);
    ASSERT_EQ (0, winner1.second);
    mu_coin::vote vote2;
    vote2.address = key2.pub;
    vote2.sequence = 1;
    vote2.block = block1;
    mu_coin::sign_message (key2.prv, key2.pub, vote2.hash (), vote2.signature);
    votes.add (vote2);
    auto uncontested2 (votes.uncontested ());
    ASSERT_EQ (std::numeric_limits <mu_coin::uint256_t>::max (), uncontested2);
    mu_coin::vote vote3;
    vote3.address = key3.pub;
    vote3.sequence = 1;
    vote3.block = block2;
    mu_coin::sign_message (key3.prv, key3.pub, vote3.hash (), vote3.signature);
    votes.add (vote3);
    auto uncontested3 (votes.uncontested ());
    ASSERT_EQ (0, uncontested3);
    mu_coin::vote vote4;
    vote4.address = key3.pub;
    vote4.sequence = 2;
    vote4.block = block1;
    mu_coin::sign_message (key3.prv, key3.pub, vote4.hash (), vote4.signature);
    votes.add (vote4);
    auto uncontested4 (votes.uncontested ());
    ASSERT_EQ (0, uncontested4);
}

TEST (votes, add_old)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::votes votes (ledger);
    mu_coin::block_hash block1 (2);
    mu_coin::block_hash block2 (3);
    ASSERT_EQ (0, votes.rep_votes.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 2;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    votes.add (vote1);
    auto winner1 (votes.winner ());
    ASSERT_EQ (block1, winner1.first);
    mu_coin::vote vote2;
    vote2.address = key1.pub;
    vote2.sequence = 1;
    vote2.block = block2;
    mu_coin::sign_message (key1.prv, key1.pub, vote2.hash (), vote2.signature);
    votes.add (vote2);
    auto winner2 (votes.winner ());
    ASSERT_EQ (block1, winner2.first);
    ASSERT_EQ (1, votes.rep_votes.size ());
    ASSERT_NE (votes.rep_votes.end (), votes.rep_votes.find (key1.pub));
    ASSERT_EQ (block1, votes.rep_votes [key1.pub].second);
}

TEST (conflicts, add_one)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::conflicts conflicts (ledger);
    mu_coin::block_hash root;
    mu_coin::block_hash block;
    ASSERT_EQ (0, conflicts.roots.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    conflicts.start (root);
    ASSERT_EQ (1, conflicts.roots.size ());
    conflicts.add (root, vote1);
    ASSERT_EQ (1, conflicts.roots.size ());
    auto existing (conflicts.roots.find (root));
    ASSERT_NE (conflicts.roots.end (), existing);
    ASSERT_EQ (1, existing->second->rep_votes.size ());
    auto existing_vote (existing->second->rep_votes.find (key1.pub));
    ASSERT_NE (existing->second->rep_votes.end (), existing_vote);
    ASSERT_EQ (block, existing_vote->second.second);
    conflicts.stop (root);
    ASSERT_EQ (0, conflicts.roots.size ());
}

TEST (conflicts, add_two)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::conflicts conflicts (ledger);
    mu_coin::block_hash root1;
    mu_coin::block_hash block1;
    mu_coin::block_hash root2;
    mu_coin::block_hash block2;
    ASSERT_EQ (0, conflicts.roots.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    conflicts.start (root1);
    conflicts.add (root1, vote1);
    mu_coin::vote vote2;
    vote2.address = key1.pub;
    vote2.sequence = 1;
    vote2.block = block2;
    mu_coin::sign_message (key1.prv, key1.pub, vote2.hash (), vote2.signature);
    conflicts.start (root2);
    conflicts.add (root2, vote2);
    ASSERT_EQ (2, conflicts.roots.size ());
    auto existing1 (conflicts.roots.find (root1));
    ASSERT_NE (conflicts.roots.end (), existing1);
    ASSERT_EQ (1, existing1->second->rep_votes.size ());
    auto existing_vote1 (existing1->second->rep_votes.find (key1.pub));
    ASSERT_NE (existing1->second->rep_votes.end (), existing_vote1);
    ASSERT_EQ (block1, existing_vote1->second.second);
    auto existing2 (conflicts.roots.find (root2));
    ASSERT_NE (conflicts.roots.end (), existing2);
    ASSERT_EQ (1, existing2->second->rep_votes.size ());
    auto existing_vote2 (existing2->second->rep_votes.find (key1.pub));
    ASSERT_NE (existing2->second->rep_votes.end (), existing_vote2);
    ASSERT_EQ (block2, existing_vote2->second.second);
}

TEST (conflicts, add_existing)
{
    mu_coin::block_store store (mu_coin::block_store_temp);
	mu_coin::keypair key1;
	mu_coin::genesis genesis (key1.pub);
	genesis.initialize (store);
    mu_coin::ledger ledger (store);
    mu_coin::conflicts conflicts (ledger);
    mu_coin::block_hash root;
    mu_coin::block_hash block1;
    mu_coin::block_hash block2;
    ASSERT_EQ (0, conflicts.roots.size ());
    mu_coin::vote vote1;
    vote1.address = key1.pub;
    vote1.sequence = 1;
    vote1.block = block1;
    mu_coin::sign_message (key1.prv, key1.pub, vote1.hash (), vote1.signature);
    conflicts.start (root);
    conflicts.add (root, vote1);
    mu_coin::vote vote2;
    vote2.address = key1.pub;
    vote2.sequence = 2;
    vote2.block = block2;
    mu_coin::sign_message (key1.prv, key1.pub, vote2.hash (), vote2.signature);
    conflicts.add (root, vote2);
    ASSERT_EQ (1, conflicts.roots.size ());
    auto existing (conflicts.roots.find (root));
    ASSERT_NE (conflicts.roots.end (), existing);
    ASSERT_EQ (1, existing->second->rep_votes.size ());
    auto existing_vote (existing->second->rep_votes.find (key1.pub));
    ASSERT_NE (existing->second->rep_votes.end (), existing_vote);
    ASSERT_EQ (block2, existing_vote->second.second);
}

TEST (ledger, successor)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
	mu_coin::keypair key1;
	mu_coin::send_block send1;
	send1.hashables.previous = system.genesis.hash ();
	send1.hashables.balance.clear ();
	send1.hashables.destination = key1.pub;
	mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send1.hash (), send1.signature);
	ASSERT_EQ (mu_coin::process_result::progress, system.clients [0]->client_m->ledger.process (send1));
	ASSERT_EQ (send1, *system.clients [0]->client_m->ledger.successor (system.genesis.hash ()));
}

TEST (fork, publish)
{
    mu_coin::system system (1, 24000, 25000, 1, std::numeric_limits <mu_coin::uint256_t>::max ());
	system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    mu_coin::keypair key1;
    std::unique_ptr <mu_coin::send_block> send1 (new mu_coin::send_block);
    send1->hashables.previous = system.genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send1->hash (), send1->signature);
    mu_coin::publish_req publish1;
    publish1.block = std::move (send1);
    mu_coin::keypair key2;
    std::unique_ptr <mu_coin::send_block> send2 (new mu_coin::send_block);
    send2->hashables.previous = system.genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send2->hash (), send2->signature);
    mu_coin::publish_req publish2;
    publish2.block = std::move (send2);
    system.clients [0]->client_m->processor.process_message (publish1, mu_coin::endpoint {}, true);
	ASSERT_EQ (0, system.clients [0]->client_m->conflicts.roots.size ());
    system.clients [0]->client_m->processor.process_message (publish2, mu_coin::endpoint {}, true);
	ASSERT_EQ (1, system.clients [0]->client_m->conflicts.roots.size ());
	ASSERT_NE (system.clients [0]->client_m->conflicts.roots.end (), system.clients [0]->client_m->conflicts.roots.find (publish1.block->previous ()));
	while (!system.clients [0]->client_m->conflicts.roots.empty ())
	{
		system.service->poll_one ();
		system.processor.poll_one ();
	}
}

TEST (fork, keep)
{
    mu_coin::system system (1, 24000, 25000, 2, std::numeric_limits <mu_coin::uint256_t>::max ());
	system.clients [0]->client_m->wallet.insert (system.test_genesis_address.prv, system.clients [0]->client_m->wallet.password);
    mu_coin::keypair key1;
    std::unique_ptr <mu_coin::send_block> send1 (new mu_coin::send_block);
    send1->hashables.previous = system.genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send1->hash (), send1->signature);
    mu_coin::publish_req publish1;
    publish1.block = std::move (send1);
    mu_coin::keypair key2;
    std::unique_ptr <mu_coin::send_block> send2 (new mu_coin::send_block);
    send2->hashables.previous = system.genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    mu_coin::sign_message (system.test_genesis_address.prv, system.test_genesis_address.pub, send2->hash (), send2->signature);
    mu_coin::publish_req publish2;
    publish2.block = std::move (send2);
    system.clients [0]->client_m->processor.process_message (publish1, mu_coin::endpoint {}, true);
	system.clients [1]->client_m->processor.process_message (publish1, mu_coin::endpoint {}, true);
    system.clients [0]->client_m->processor.process_message (publish2, mu_coin::endpoint {}, true);
	system.clients [1]->client_m->processor.process_message (publish2, mu_coin::endpoint {}, true);
	while (system.clients [0]->client_m->conflicts.roots.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
	}
	ASSERT_TRUE (system.clients [0]->client_m->store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (system.clients [1]->client_m->store.block_exists (publish1.block->hash ()));
}