#include <gtest/gtest.h>
#include <rai/core/core.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

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
    rai::account account;
    auto balance (ledger.account_balance (account));
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
    auto balance (ledger.account_balance (rai::genesis_account));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), balance);
    rai::frontier frontier;
    ASSERT_FALSE (store.latest_get (rai::genesis_account, frontier));
    ASSERT_GE (store.now (), frontier.time);
    ASSERT_LT (store.now () - frontier.time, 10);
}

TEST (ledger, checksum_persistence)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    rai::uint256_union checksum1;
    rai::uint256_union max;
    max.qwords [0] = 0;
    max.qwords [0] = ~max.qwords [0];
    max.qwords [1] = 0;
    max.qwords [1] = ~max.qwords [1];
    max.qwords [2] = 0;
    max.qwords [2] = ~max.qwords [2];
    max.qwords [3] = 0;
    max.qwords [3] = ~max.qwords [3];
    {
        bool init1;
        rai::ledger ledger (init1, init, store);
        ASSERT_FALSE (init1);
        rai::genesis genesis;
        genesis.initialize (store);
        checksum1 = ledger.checksum (0, max);
    }
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    ASSERT_EQ (checksum1, ledger.checksum (0, max));
}

TEST (system, system_genesis)
{
    rai::system system (24000, 2);
    for (auto & i: system.clients)
    {
        ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), i->ledger.account_balance (rai::genesis_account));
    }
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
    rai::account account1;
    rai::amount amount1;
    ledger.send_observer = [&account1, &amount1, &send] (rai::send_block const & block_a, rai::account const & account_a, rai::amount const & amount_a)
    {
        account1 = account_a;
        amount1 = amount_a;
        ASSERT_EQ (send, block_a);
    };
    ASSERT_EQ (rai::process_result::progress, ledger.process (send));
    ASSERT_EQ (rai::test_genesis_key.pub, account1);
    ASSERT_EQ (rai::amount (50), amount1);
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.weight (key2.pub));
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
    rai::receivable receivable1;
	ASSERT_FALSE (ledger.store.pending_get (hash1, receivable1));
    ASSERT_EQ (rai::test_genesis_key.pub, receivable1.source);
    ASSERT_EQ (key2.pub, receivable1.destination);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, receivable1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier6;
	ASSERT_FALSE (ledger.store.latest_get (rai::test_genesis_key.pub, frontier6));
	ASSERT_EQ (hash1, frontier6.hash);
	ledger.rollback (frontier6.hash);
    rai::frontier frontier7;
	ASSERT_FALSE (ledger.store.latest_get (rai::test_genesis_key.pub, frontier7));
	ASSERT_EQ (frontier1.hash, frontier7.hash);
    rai::receivable receivable2;
	ASSERT_TRUE (ledger.store.pending_get (hash1, receivable2));
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.account_balance (rai::test_genesis_key.pub));
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
    rai::account account2;
    rai::amount amount2;
    rai::account account3;
    ledger.open_observer = [&account2, &amount2, &account3, &open] (rai::open_block const & block_a, rai::account const & account_a, rai::amount const & amount_a, rai::account const & representative_a)
    {
        account2 = account_a;
        amount2 = amount_a;
        account3 = representative_a;
        ASSERT_EQ (open, block_a);
    };
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    ASSERT_EQ (key2.pub, account2);
    ASSERT_EQ (rai::amount (std::numeric_limits <rai::uint128_t>::max () - 50), amount2);
    ASSERT_EQ (key3.pub, account3);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.weight (key3.pub));
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
    rai::account account1;
    rai::amount amount1;
    ledger.receive_observer = [&account1, &amount1, &receive] (rai::receive_block const & block_a, rai::account const & account_a, rai::amount const & amount_a)
    {
        account1 = account_a;
        amount1 = amount_a;
        ASSERT_EQ (receive, block_a);
    };
	ASSERT_EQ (rai::process_result::progress, ledger.process (receive));
    ASSERT_EQ (rai::uint128_union (std::numeric_limits <rai::uint128_t>::max () - 25), amount1);
    ASSERT_EQ (key2.pub, account1);
	ASSERT_EQ (hash4, ledger.latest (key2.pub));
	ASSERT_EQ (25, ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 25, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 25, ledger.weight (key3.pub));
	ledger.rollback (hash4);
	ASSERT_EQ (25, ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
    rai::receivable receivable1;
	ASSERT_FALSE (ledger.store.pending_get (hash3, receivable1));
    ASSERT_EQ (rai::test_genesis_key.pub, receivable1.source);
    ASSERT_EQ (25, receivable1.amount.number ());
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
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.weight (key3.pub));
	ledger.rollback (hash1);
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.account_balance (rai::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
	rai::frontier frontier2;
	ASSERT_TRUE (ledger.store.latest_get (key2.pub, frontier2));
    rai::receivable receivable1;
	ASSERT_TRUE (ledger.store.pending_get (frontier2.hash, receivable1));
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
    rai::change_block change1 (key5.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    ASSERT_EQ (rai::process_result::progress, ledger.process (change1));
    rai::keypair key3;
    rai::change_block change2 (key3.pub, change1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 1, ledger.weight (key4.pub));
    ledger.rollback (receive1.hash ());
    ASSERT_EQ (50, ledger.weight (key3.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 50, ledger.weight (key4.pub));
    ledger.rollback (open.hash ());
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (key3.pub));
    ASSERT_EQ (0, ledger.weight (key4.pub));
    ledger.rollback (change2.hash ());
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (key5.pub));
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
    open.hashables.representative.clear ();
    open.hashables.source = hash1;
    rai::block_hash hash2 (open.hash ());
    rai::sign_message(key2.prv, key2.pub, hash2, open.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (open));
    ASSERT_EQ (rai::process_result::old, ledger.process (open));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (rai::test_genesis_key.pub));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::change_block block (key2.pub, frontier1.hash, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    rai::account account1;
    rai::account account2;
    ledger.change_observer = [&account1, &account2, &block] (rai::change_block const & block_a, rai::account const & account_a, rai::account const & representative_a)
    {
        account1 = account_a;
        account2 = representative_a;
        ASSERT_EQ (block, block_a);
    };
    ASSERT_EQ (rai::process_result::progress, ledger.process (block));
    ASSERT_EQ (rai::test_genesis_key.pub, account1);
    ASSERT_EQ (key2.pub, account2);
    ASSERT_EQ (0, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (key2.pub));
	rai::frontier frontier2;
	ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier2));
	ASSERT_EQ (block.hash (), frontier2.hash);
	ledger.rollback (frontier2.hash);
	rai::frontier frontier3;
	ASSERT_FALSE (store.latest_get (rai::test_genesis_key.pub, frontier3));
	ASSERT_EQ (frontier1.hash, frontier3.hash);
	ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), ledger.weight (rai::test_genesis_key.pub));
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
    ASSERT_EQ (rai::process_result::fork_previous, ledger.process (block2));
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
    rai::change_block block3 (key3.pub, block2.hash (), 0, key2.prv, key2.pub);
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
    ASSERT_EQ (rai::process_result::fork_previous, ledger.process (block5));
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
    rai::change_block block1 (rai::account (0), ledger.latest (rai::test_genesis_key.pub), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
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
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    system.generate_send_existing (*system.clients [0]);
    rai::frontier frontier2;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier2));
    ASSERT_NE (frontier1.hash, frontier2.hash);
    auto iterations1 (0);
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) == std::numeric_limits <rai::uint128_t>::max ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 20);
    }
    auto iterations2 (0);
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) != std::numeric_limits <rai::uint128_t>::max ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 20);
    }
}

TEST (system, generate_send_new)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    auto iterator1 (system.clients [0]->store.latest_begin ());
    ++iterator1;
    ASSERT_EQ (system.clients [0]->store.latest_end (), iterator1);
    system.generate_send_new (*system.clients [0]);
    rai::account new_account;
    auto iterator2 (system.wallet (0)->store.begin ());
    if (iterator2->first != rai::test_genesis_key.pub)
    {
        new_account = iterator2->first;
    }
    ++iterator2;
    ASSERT_NE (system.wallet (0)->store.end (), iterator2);
    if (iterator2->first != rai::test_genesis_key.pub)
    {
        new_account = iterator2->first;
    }
    ++iterator2;
    ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
    while (system.clients [0]->ledger.account_balance (new_account) == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
    }
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key2;
    rai::send_block block1;
    block1.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 100;
    block1.hashables.destination = key2.pub;
    block1.hashables.previous = genesis.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block1));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key3;
    rai::open_block block2;
    block2.hashables.representative = key3.pub;
    block2.hashables.source = block1.hash ();
    rai::sign_message (key2.prv, key2.pub, block2.hash (), block2.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block2));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::send_block block3;
    block3.hashables.balance = std::numeric_limits <rai::uint128_t>::max () - 200;
    block3.hashables.destination = key2.pub;
    block3.hashables.previous = block1.hash ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block3.hash (), block3.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block3));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::receive_block block4;
    block4.hashables.previous = block2.hash ();
    block4.hashables.source = block3.hash ();
    rai::sign_message (key2.prv, key2.pub, block4.hash (), block4.signature);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block4));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (200, store.representation_get (key3.pub));
    rai::keypair key4;
    rai::change_block block5 (key4.pub, block4.hash (), 0, key2.prv, key2.pub);
    ASSERT_EQ (rai::process_result::progress, ledger.process (block5));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
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
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 200, store.representation_get (rai::test_genesis_key.pub));
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
    ASSERT_EQ (rai::process_result::fork_source, ledger.process (open2));
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
    auto votes1 (client1.conflicts.roots.find (send1.root ())->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.account = key1.pub;
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
    auto votes1 (client1.conflicts.roots.find (send1.root ())->second);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.account = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    auto existing1 (votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_NE (votes1->votes.rep_votes.end (), existing1);
    ASSERT_EQ (send1, *existing1->second.second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send1, *winner.second);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), winner.first);
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
    auto votes1 (client1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.account = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.account = key2.pub;
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
    ASSERT_EQ (send1, *winner.second);
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
    auto votes1 (client1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1;
    vote1.sequence = 1;
    vote1.block = send1.clone ();
    vote1.account = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.account = rai::test_genesis_key.pub;
    vote2.sequence = 2;
    vote2.block = send2.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote2.hash (), vote2.signature);
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send2, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send2, *winner.second);
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
    auto votes1 (client1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1;
    vote1.sequence = 2;
    vote1.block = send1.clone ();
    vote1.account = rai::test_genesis_key.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, vote1.hash (), vote1.signature);
    votes1->vote (vote1);
    rai::send_block send2;
    rai::keypair key2;
    send2.hashables.previous = genesis.hash ();
    send2.hashables.balance.clear ();
    send2.hashables.destination = key2.pub;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2.hash (), send2.signature);
    rai::vote vote2;
    vote2.account = rai::test_genesis_key.pub;
    vote2.sequence = 1;
    vote2.block = send2.clone ();
    rai::sign_message (key2.prv, key2.pub, vote2.hash (), vote2.signature);
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send1, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (send1, *winner.second);
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
        system.wallet (0)->store.insert (rai::test_genesis_key.prv);
        rai::keypair key1;
		rai::genesis genesis;
        std::unique_ptr <rai::send_block> send1 (new rai::send_block);
        send1->hashables.previous = genesis.hash ();
        send1->hashables.balance.clear ();
        send1->hashables.destination = key1.pub;
        rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
        rai::publish publish1;
        publish1.block = std::move (send1);
        rai::keypair key2;
        std::unique_ptr <rai::send_block> send2 (new rai::send_block);
        send2->hashables.previous = genesis.hash ();
        send2->hashables.balance.clear ();
        send2->hashables.destination = key2.pub;
        rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
        rai::publish publish2;
        publish2.block = std::move (send2);
        client1.processor.process_message (publish1, client1.network.endpoint ());
        ASSERT_EQ (0, client1.conflicts.roots.size ());
        client1.processor.process_message (publish2, client1.network.endpoint ());
        ASSERT_EQ (1, client1.conflicts.roots.size ());
        auto conflict1 (client1.conflicts.roots.find (publish1.block->root ()));
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
        ASSERT_EQ (*publish1.block, *winner.second);
        ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), winner.first);
    }
    ASSERT_TRUE (client0.expired ());
}

TEST (ledger, fork_keep)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
	ASSERT_EQ (1, client1.peers.size ());
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    send1->work = client1.create_work (*send1);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    send2->work = client1.create_work (*send2);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish publish2;
    publish2.block = std::move (send2);
    client1.processor.process_message (publish1, client1.network.endpoint ());
	client2.processor.process_message (publish1, client2.network.endpoint ());
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, client1.network.endpoint ());
	client2.processor.process_message (publish2, client2.network.endpoint ());
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    ASSERT_EQ (1, client2.conflicts.roots.size ());
    auto conflict (client2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (client2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	ASSERT_TRUE (system.clients [0]->store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (system.clients [1]->store.block_exists (publish1.block->hash ()));
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
	}
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), winner.first);
	ASSERT_TRUE (system.clients [0]->store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (system.clients [1]->store.block_exists (publish1.block->hash ()));
}

TEST (ledger, fork_flip)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
    ASSERT_EQ (1, client1.peers.size ());
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    send1->work = client1.create_work (*send1);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    send2->work = client1.create_work (*send2);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish publish2;
    publish2.block = std::move (send2);
    client1.processor.process_message (publish1, client1.network.endpoint ());
    client2.processor.process_message (publish2, client1.network.endpoint ());
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, client1.network.endpoint ());
    client2.processor.process_message (publish1, client2.network.endpoint ());
    ASSERT_EQ (1, client1.conflicts.roots.size ());
    ASSERT_EQ (1, client2.conflicts.roots.size ());
    auto conflict (client2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (client2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
    ASSERT_TRUE (client2.store.block_exists (publish2.block->hash ()));
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), winner.first);
    ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
    ASSERT_TRUE (client2.store.block_exists (publish1.block->hash ()));
    ASSERT_FALSE (client2.store.block_exists (publish2.block->hash ()));
}

TEST (ledger, fork_multi_flip)
{
    rai::system system (24000, 2);
    auto & client1 (*system.clients [0]);
    auto & client2 (*system.clients [1]);
	ASSERT_EQ (1, client1.peers.size ());
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = genesis.hash ();
    send1->hashables.balance.clear ();
    send1->hashables.destination = key1.pub;
    send1->work = client1.create_work (*send1);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = genesis.hash ();
    send2->hashables.balance.clear ();
    send2->hashables.destination = key2.pub;
    send2->work = client1.create_work (*send2);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    rai::publish publish2;
    publish2.block = std::move (send2);
    std::unique_ptr <rai::send_block> send3 (new rai::send_block);
    send3->hashables.previous = publish2.block->hash ();
    send3->hashables.balance.clear ();
    send3->hashables.destination = key2.pub;
    send3->work = client1.create_work (*send3);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send3->hash (), send3->signature);
    rai::publish publish3;
    publish3.block = std::move (send3);
    client1.processor.process_message (publish1, client1.network.endpoint ());
	client2.processor.process_message (publish2, client2.network.endpoint ());
    client2.processor.process_message (publish3, client2.network.endpoint ());
    ASSERT_EQ (0, client1.conflicts.roots.size ());
    ASSERT_EQ (0, client2.conflicts.roots.size ());
    client1.processor.process_message (publish2, client1.network.endpoint ());
    client1.processor.process_message (publish3, client1.network.endpoint ());
	client2.processor.process_message (publish1, client2.network.endpoint ());
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
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
	}
    auto winner (votes1->votes.winner ());
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), winner.first);
	ASSERT_TRUE (client1.store.block_exists (publish1.block->hash ()));
	ASSERT_TRUE (client2.store.block_exists (publish1.block->hash ()));
	ASSERT_FALSE (client2.store.block_exists (publish2.block->hash ()));
    ASSERT_FALSE (client2.store.block_exists (publish3.block->hash ()));
}

TEST (ledger, fail_change_old)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key1;
    rai::change_block block (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::progress, result1);
    auto result2 (ledger.process (block));
    ASSERT_EQ (rai::process_result::old, result2);
}

TEST (ledger, fail_change_gap_previous)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key1;
    rai::change_block block (key1.pub, 1, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::gap_previous, result1);
}

TEST (ledger, fail_change_bad_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key1;
    rai::change_block block (key1.pub, genesis.hash (), 0, rai::private_key (0), rai::public_key (0));
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::bad_signature, result1);
}

TEST (ledger, fail_change_fork)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key1;
    rai::change_block block1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::keypair key2;
    rai::change_block block2 (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::fork_previous, result2);
}

TEST (ledger, fail_send_old)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block;
    rai::keypair key1;
    block.hashables.destination = key1.pub;
    block.hashables.previous = genesis.hash ();
    block.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block.hash (), block.signature);
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::progress, result1);
    auto result2 (ledger.process (block));
    ASSERT_EQ (rai::process_result::old, result2);
}

TEST (ledger, fail_send_gap_previous)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block;
    rai::keypair key1;
    block.hashables.destination = key1.pub;
    block.hashables.previous = 1;
    block.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block.hash (), block.signature);
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::gap_previous, result1);
}

TEST (ledger, fail_send_bad_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block;
    rai::keypair key1;
    block.hashables.destination = key1.pub;
    block.hashables.previous = genesis.hash ();
    block.hashables.balance = 1;
    block.signature.clear ();
    auto result1 (ledger.process (block));
    ASSERT_EQ (rai::process_result::bad_signature, result1);
}

TEST (ledger, fail_send_overspend)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    rai::keypair key2;
    block2.hashables.destination = key2.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance = 2;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::overspend, result2);
}

TEST (ledger, fail_send_fork)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    rai::keypair key2;
    block2.hashables.destination = key2.pub;
    block2.hashables.previous = genesis.hash ();
    block2.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::fork_previous, result2);
}

TEST (ledger, fail_open_old)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::open_block block2;
    block2.hashables.representative.clear ();
    block2.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    auto result3 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::old, result3);
}

TEST (ledger, fail_open_gap_source)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::keypair key1;
    rai::open_block block2;
    block2.hashables.representative.clear ();
    block2.hashables.source = 1;
    rai::sign_message (key1.prv, key1.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::gap_source, result2);
}

TEST (ledger, fail_open_overreceive)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::open_block block2;
    block2.hashables.representative.clear ();
    block2.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative = 1;
    block3.hashables.source = block1.hash ();;
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::fork_source, result3);
}

TEST (ledger, fail_open_bad_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::open_block block2;
    block2.hashables.representative.clear ();
    block2.hashables.source = block1.hash ();
    block2.signature.clear ();
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::bad_signature, result2);
}

TEST (ledger, fail_open_fork_previous)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::open_block block4;
    block4.hashables.representative.clear ();
    block4.hashables.source = block2.hash ();
    rai::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::fork_previous, result4);
}

TEST (ledger, fail_receive_old)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4;
    block4.hashables.previous = block3.hash ();
    block4.hashables.source = block2.hash ();
    rai::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::progress, result4);
    auto result5 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::old, result5);
}

TEST (ledger, fail_receive_gap_source)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4;
    block4.hashables.previous = block3.hash ();
    block4.hashables.source = 1;
    rai::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::gap_source, result4);
}

TEST (ledger, fail_receive_overreceive)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::open_block block2;
    block2.hashables.representative.clear ();
    block2.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block2.hash (), block2.signature);
    auto result3 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block3;
    block3.hashables.previous = block2.hash ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result4 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::overreceive, result4);
}

TEST (ledger, fail_receive_bad_signature)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4;
    block4.hashables.previous = block3.hash ();
    block4.hashables.source = block2.hash ();
    block4.signature.clear ();
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::bad_signature, result4);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4;
    block4.hashables.previous = 1;
    block4.hashables.source = block2.hash ();
    rai::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::gap_previous, result4);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::receive_block block3;
    block3.hashables.previous = 1;
    block3.hashables.source = block2.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::gap_previous, result3);
}

TEST (ledger, fail_receive_fork_previous)
{
    leveldb::Status init;
    rai::block_store store (init, rai::block_store_temp);
    ASSERT_TRUE (init.ok ());
    bool init1;
    rai::ledger ledger (init1, init, store);
    ASSERT_FALSE (init1);
    rai::genesis genesis;
    genesis.initialize (store);
    rai::send_block block1;
    rai::keypair key1;
    block1.hashables.destination = key1.pub;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance = 1;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    auto result1 (ledger.process (block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2;
    block2.hashables.destination = key1.pub;
    block2.hashables.previous = block1.hash ();
    block2.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block2.hash (), block2.signature);
    auto result2 (ledger.process (block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3;
    block3.hashables.representative.clear ();
    block3.hashables.source = block1.hash ();
    rai::sign_message (key1.prv, key1.pub, block3.hash (), block3.signature);
    auto result3 (ledger.process (block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::send_block block4;
    rai::keypair key2;
    block4.hashables.destination = key1.pub;
    block4.hashables.previous = block3.hash ();
    block4.hashables.balance = 1;
    rai::sign_message (key1.prv, key1.pub, block4.hash (), block4.signature);
    auto result4 (ledger.process (block4));
    ASSERT_EQ (rai::process_result::progress, result4);
    rai::receive_block block5;
    block5.hashables.previous = block3.hash ();
    block5.hashables.source = block2.hash ();
    rai::sign_message (key1.prv, key1.pub, block5.hash (), block5.signature);
    auto result5 (ledger.process (block5));
    ASSERT_EQ (rai::process_result::fork_previous, result5);
}