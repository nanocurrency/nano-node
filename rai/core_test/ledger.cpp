#include <gtest/gtest.h>
#include <rai/node.hpp>
#include <cryptopp/filters.h>
#include <cryptopp/randpool.h>

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
    bool init;
    rai::block_store store (init, boost::filesystem::path {});
    ASSERT_FALSE (!init);
    rai::ledger ledger (store);
}

// Ledger can be initialized and retuns a basic query for an empty account
TEST (ledger, empty)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::account account;
	rai::transaction transaction (store.environment, nullptr, false);
    auto balance (ledger.account_balance (transaction, account));
    ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    auto balance (ledger.account_balance (transaction, rai::genesis_account));
    ASSERT_EQ (rai::genesis_amount, balance);
    rai::frontier frontier;
    ASSERT_FALSE (store.latest_get (transaction, rai::genesis_account, frontier));
	// Frontier time should have been updated when genesis balance was added
    ASSERT_GE (store.now (), frontier.time);
    ASSERT_LT (store.now () - frontier.time, 10);
}

// Make sure the checksum is the same when ledger reloaded
TEST (ledger, checksum_persistence)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
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
	rai::transaction transaction (store.environment, nullptr, true);
	{
		rai::ledger ledger (store);
		rai::genesis genesis;
		genesis.initialize (transaction, store);
		checksum1 = ledger.checksum (transaction, 0, max);
	}
	rai::ledger ledger (store);
    ASSERT_EQ (checksum1, ledger.checksum (transaction, 0, max));
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
    rai::system system (24000, 2);
    for (auto & i: system.nodes)
    {
		rai::transaction transaction (i->store.environment, nullptr, false);
        ASSERT_EQ (rai::genesis_amount, i->ledger.account_balance (transaction, rai::genesis_account));
    }
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
	rai::transaction transaction (store.environment, nullptr, true);
	rai::genesis genesis;
	genesis.initialize (transaction, store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::keypair key2;
    rai::send_block send (key2.pub, frontier1.hash, 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::block_hash hash1 (send.hash ());
    rai::account account1;
    rai::amount amount1;
	// Hook the send observer and make sure it's the one we just created
    ledger.send_observer = [&account1, &amount1, &send] (rai::send_block const & block_a, rai::account const & account_a, rai::amount const & amount_a)
    {
	    account1 = account_a;
        amount1 = amount_a;
        ASSERT_EQ (send, block_a);
    };
	// This was a valid block, it should progress.
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send));
	// Account from send observer should have been the genesis account
    ASSERT_EQ (rai::test_genesis_key.pub, account1);
	// Amount from the send observer should be how much we just sent
    ASSERT_EQ (rai::amount (50), amount1);
    ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier2));
    auto latest6 (store.block_get (transaction, frontier2.hash));
    ASSERT_NE (nullptr, latest6);
    auto latest7 (dynamic_cast <rai::send_block *> (latest6.get ()));
    ASSERT_NE (nullptr, latest7);
    ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
    rai::open_block open (key2.pub, key2.pub, hash1, key2.prv, key2.pub, rai::work_generate (key2.pub));
    rai::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open));
    ASSERT_EQ (rai::genesis_amount - 50, ledger.account_balance (key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (rai::genesis_amount - 50, ledger.weight (key2.pub));
    rai::frontier frontier3;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier3));
    auto latest2 (store.block_get (transaction, frontier3.hash));
    ASSERT_NE (nullptr, latest2);
    auto latest3 (dynamic_cast <rai::send_block *> (latest2.get ()));
    ASSERT_NE (nullptr, latest3);
    ASSERT_EQ (send, *latest3);
    rai::frontier frontier4;
    ASSERT_FALSE (store.latest_get (transaction, key2.pub, frontier4));
    auto latest4 (store.block_get (transaction, frontier4.hash));
    ASSERT_NE (nullptr, latest4);
    auto latest5 (dynamic_cast <rai::open_block *> (latest4.get ()));
    ASSERT_NE (nullptr, latest5);
    ASSERT_EQ (open, *latest5);
	ledger.rollback (hash2);
	rai::frontier frontier5;
	ASSERT_TRUE (ledger.store.latest_get (transaction, key2.pub, frontier5));
    rai::receivable receivable1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, hash1, receivable1));
    ASSERT_EQ (rai::test_genesis_key.pub, receivable1.source);
    ASSERT_EQ (key2.pub, receivable1.destination);
    ASSERT_EQ (rai::genesis_amount - 50, receivable1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (rai::genesis_amount, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier6;
	ASSERT_FALSE (ledger.store.latest_get (transaction, rai::test_genesis_key.pub, frontier6));
	ASSERT_EQ (hash1, frontier6.hash);
	ledger.rollback (frontier6.hash);
    rai::frontier frontier7;
	ASSERT_FALSE (ledger.store.latest_get (transaction, rai::test_genesis_key.pub, frontier7));
	ASSERT_EQ (frontier1.hash, frontier7.hash);
    rai::receivable receivable2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, hash1, receivable2));
	ASSERT_EQ (rai::genesis_amount, ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (ledger, process_receive)
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
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send));
    rai::keypair key3;
    rai::open_block open (key2.pub, key3.pub, hash1, key2.prv, key2.pub, rai::work_generate (key2.pub));
    rai::block_hash hash2 (open.hash ());
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
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open));
    ASSERT_EQ (key2.pub, account2);
    ASSERT_EQ (rai::amount (rai::genesis_amount - 50), amount2);
    ASSERT_EQ (key3.pub, account3);
    ASSERT_EQ (rai::genesis_amount - 50, ledger.weight (key3.pub));
	rai::send_block send2 (key2.pub, hash1, 25, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::block_hash hash3 (send2.hash ());
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send2));
	rai::receive_block receive (hash2, hash3, key2.prv, key2.pub, 0);
	auto hash4 (receive.hash ());
    rai::account account1;
    rai::amount amount1;
    ledger.receive_observer = [&account1, &amount1, &receive] (rai::receive_block const & block_a, rai::account const & account_a, rai::amount const & amount_a)
    {
        account1 = account_a;
        amount1 = amount_a;
        ASSERT_EQ (receive, block_a);
    };
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive));
    ASSERT_EQ (rai::uint128_union (rai::genesis_amount - 25), amount1);
    ASSERT_EQ (key2.pub, account1);
	ASSERT_EQ (hash4, ledger.latest (key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, rai::test_genesis_key.pub));
	ASSERT_EQ (rai::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
    ASSERT_EQ (rai::genesis_amount - 25, ledger.weight (key3.pub));
	ledger.rollback (hash4);
	ASSERT_EQ (25, ledger.account_balance (transaction, rai::test_genesis_key.pub));
	ASSERT_EQ (rai::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
    ASSERT_EQ (rai::genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
    rai::receivable receivable1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, hash3, receivable1));
    ASSERT_EQ (rai::test_genesis_key.pub, receivable1.source);
    ASSERT_EQ (25, receivable1.amount.number ());
}

TEST (ledger, rollback_receiver)
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
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send));
    rai::keypair key3;
    rai::open_block open (key2.pub, key3.pub, hash1, key2.prv, key2.pub, rai::work_generate (key2.pub));
    rai::block_hash hash2 (open.hash ());
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open));
	ASSERT_EQ (hash2, ledger.latest (key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, rai::test_genesis_key.pub));
	ASSERT_EQ (rai::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
    ASSERT_EQ (50, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (rai::genesis_amount - 50, ledger.weight (key3.pub));
	ledger.rollback (hash1);
	ASSERT_EQ (rai::genesis_amount, ledger.account_balance (transaction, rai::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
    ASSERT_EQ (rai::genesis_amount, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
	rai::frontier frontier2;
	ASSERT_TRUE (ledger.store.latest_get (transaction, key2.pub, frontier2));
    rai::receivable receivable1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, frontier2.hash, receivable1));
}

TEST (ledger, rollback_representation)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key5;
    rai::change_block change1 (key5.pub, genesis.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, change1));
    rai::keypair key3;
    rai::change_block change2 (key3.pub, change1.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, change2));
    rai::keypair key2;
    rai::send_block send1 (key2.pub, change2.hash (), 50, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send1));
    rai::keypair key4;
    rai::open_block open (key2.pub, key4.pub, send1.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open));
    rai::send_block send2 (key2.pub, send1.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send2));
    rai::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, receive1));
    ASSERT_EQ (1, ledger.weight (key3.pub));
    ASSERT_EQ (rai::genesis_amount - 1, ledger.weight (key4.pub));
    ledger.rollback (receive1.hash ());
    ASSERT_EQ (50, ledger.weight (key3.pub));
    ASSERT_EQ (rai::genesis_amount - 50, ledger.weight (key4.pub));
    ledger.rollback (open.hash ());
    ASSERT_EQ (rai::genesis_amount, ledger.weight (key3.pub));
    ASSERT_EQ (0, ledger.weight (key4.pub));
    ledger.rollback (change2.hash ());
    ASSERT_EQ (rai::genesis_amount, ledger.weight (key5.pub));
    ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, process_duplicate)
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
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send));
    ASSERT_EQ (rai::process_result::old, ledger.process (transaction, send));
    rai::open_block open (key2.pub, 0, hash1, key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open));
    ASSERT_EQ (rai::process_result::old, ledger.process (transaction, open));
}

TEST (ledger, representative_genesis)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
	auto latest (ledger.latest (rai::test_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
    ASSERT_EQ (rai::test_genesis_key.pub, ledger.representative (latest));
}

TEST (ledger, weight)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    ASSERT_EQ (rai::genesis_amount, ledger.weight (rai::genesis_account));
}

TEST (ledger, representative_change)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::keypair key2;
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    ASSERT_EQ (rai::genesis_amount, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (0, ledger.weight (key2.pub));
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::change_block block (key2.pub, frontier1.hash, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::account account1;
    rai::account account2;
    ledger.change_observer = [&account1, &account2, &block] (rai::change_block const & block_a, rai::account const & account_a, rai::account const & representative_a)
    {
        account1 = account_a;
        account2 = representative_a;
        ASSERT_EQ (block, block_a);
    };
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block));
    ASSERT_EQ (rai::test_genesis_key.pub, account1);
    ASSERT_EQ (key2.pub, account2);
    ASSERT_EQ (0, ledger.weight (rai::test_genesis_key.pub));
    ASSERT_EQ (rai::genesis_amount, ledger.weight (key2.pub));
	rai::frontier frontier2;
	ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier2));
	ASSERT_EQ (block.hash (), frontier2.hash);
	ledger.rollback (frontier2.hash);
	rai::frontier frontier3;
	ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier3));
	ASSERT_EQ (frontier1.hash, frontier3.hash);
	ASSERT_EQ (rai::genesis_amount, ledger.weight (rai::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::keypair key2;
    rai::keypair key3;
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::send_block block (key2.pub, frontier1.hash, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block));
    rai::send_block block2 (key3.pub, frontier1.hash, 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::fork, ledger.process (transaction, block2));
}

TEST (ledger, receive_fork)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::keypair key2;
    rai::keypair key3;
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::send_block block (key2.pub, frontier1.hash, 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block));
    rai::open_block block2 (key2.pub, key2.pub, block.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    rai::change_block block3 (key3.pub, block2.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block3));
    rai::send_block block4 (key2.pub, block.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block4));
    rai::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::fork, ledger.process (transaction, block5));
}

TEST (ledger, checksum_single)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::ledger ledger (store);
    store.checksum_put (transaction, 0, 0, genesis.hash ());
	ASSERT_EQ (genesis.hash (), ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
    rai::change_block block1 (rai::account (0), ledger.latest (rai::test_genesis_key.pub), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
	ASSERT_EQ (genesis.hash (), check1);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::checksum check2 (ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_EQ (block1.hash (), check2);
}

TEST (ledger, checksum_two)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::ledger ledger (store);
    store.checksum_put (transaction, 0, 0, genesis.hash ());
	rai::keypair key2;
    rai::send_block block1 (key2.pub, ledger.latest (rai::test_genesis_key.pub), 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
	rai::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
	rai::open_block block2 (key2.pub, 0, block1.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
	rai::checksum check2 (ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
	ASSERT_EQ (check1, check2 ^ block2.hash ());
}

TEST (ledger, DISABLED_checksum_range)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
	rai::ledger ledger (store);
	rai::transaction transaction (store.environment, nullptr, false);
    rai::checksum check1 (ledger.checksum (transaction, 0, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_TRUE (check1.is_zero ());
    rai::block_hash hash1 (42);
    rai::checksum check2 (ledger.checksum (transaction, 0, 42));
    ASSERT_TRUE (check2.is_zero ());
    rai::checksum check3 (ledger.checksum (transaction, 42, std::numeric_limits <rai::uint256_t>::max ()));
    ASSERT_EQ (hash1, check3);
}

TEST (system, generate_send_existing)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
	rai::transaction transaction (system.wallet (0)->store.environment, nullptr, false);
    ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    system.generate_send_existing (*system.nodes [0]);
    rai::frontier frontier2;
    ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier2));
    ASSERT_NE (frontier1.hash, frontier2.hash);
    auto iterations1 (0);
    while (system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub) == rai::genesis_amount)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 20);
    }
    auto iterations2 (0);
    while (system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub) != rai::genesis_amount)
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
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    auto iterator1 (system.nodes [0]->store.latest_begin (transaction));
    ++iterator1;
    ASSERT_EQ (system.nodes [0]->store.latest_end (), iterator1);
    system.generate_send_new (*system.nodes [0]);
    rai::account new_account;
    auto iterator2 (system.wallet (0)->store.begin (transaction));
    if (rai::uint256_union (iterator2->first) != rai::test_genesis_key.pub)
    {
        new_account = iterator2->first;
    }
    ++iterator2;
    ASSERT_NE (system.wallet (0)->store.end (), iterator2);
    if (rai::uint256_union (iterator2->first) != rai::test_genesis_key.pub)
    {
        new_account = iterator2->first;
    }
    ++iterator2;
    ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
    auto iterations (0);
    while (system.nodes [0]->ledger.account_balance (transaction, new_account) == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
}

TEST (ledger, representation)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    ASSERT_EQ (rai::genesis_amount, store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key2;
    rai::send_block block1 (key2.pub, genesis.hash (), rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    ASSERT_EQ (rai::genesis_amount, store.representation_get (rai::test_genesis_key.pub));
    rai::keypair key3;
    rai::open_block block2 (key2.pub, key3.pub, block1.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    ASSERT_EQ (rai::genesis_amount - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::send_block block3 (key2.pub, block1.hash (), rai::genesis_amount - 200, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block3));
    ASSERT_EQ (rai::genesis_amount - 100, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (100, store.representation_get (key3.pub));
    rai::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block4));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (200, store.representation_get (key3.pub));
    rai::keypair key4;
    rai::change_block block5 (key4.pub, block4.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block5));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    rai::keypair key5;
    rai::send_block block6 (key5.pub, block5.hash (), 100, key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block6));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (200, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    rai::keypair key6;
    rai::open_block block7 (key5.pub, key6.pub, block6.hash (), key5.prv, key5.pub, rai::work_generate (key5.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block7));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    rai::send_block block8 (key5.pub, block6.hash (), 0, key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block8));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (100, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (100, store.representation_get (key6.pub));
    rai::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block9));
    ASSERT_EQ (rai::genesis_amount - 200, store.representation_get (rai::test_genesis_key.pub));
    ASSERT_EQ (0, store.representation_get (key2.pub));
    ASSERT_EQ (0, store.representation_get (key3.pub));
    ASSERT_EQ (0, store.representation_get (key4.pub));
    ASSERT_EQ (0, store.representation_get (key5.pub));
    ASSERT_EQ (200, store.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key2;
    rai::send_block send1 (key2.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send1));
    rai::open_block open1 (key2.pub, key2.pub, send1.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open1));
    rai::open_block open2 (key2.pub, rai::test_genesis_key.pub, send1.hash (), key2.pub, key2.prv, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::unreceivable, ledger.process (transaction, open2));
}

TEST (ledegr, double_receive)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key2;
    rai::send_block send1 (key2.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send1));
    rai::open_block open1 (key2.pub, key2.pub, send1.hash (), key2.prv, key2.pub, rai::work_generate (key2.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, open1));
    rai::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, 0);
    ASSERT_EQ (rai::process_result::unreceivable, ledger.process (transaction, receive1));
}

TEST (votes, add_unsigned)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (node1.store.environment, nullptr, true);
    ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send1));
    node1.conflicts.start (send1, false);
    auto votes1 (node1.conflicts.roots.find (send1.root ())->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1 (key1.pub, 0, 1, send1.clone ());
    votes1->vote (vote1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
}

TEST (votes, add_one)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (node1.store.environment, nullptr, true);
    ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send1));
    node1.conflicts.start (send1, false);
    auto votes1 (node1.conflicts.roots.find (send1.root ())->second);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
    rai::vote vote1 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 1, send1.clone ());
    votes1->vote (vote1);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    auto existing1 (votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_NE (votes1->votes.rep_votes.end (), existing1);
    ASSERT_EQ (send1, *existing1->second.second);
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (send1, *winner.second);
    ASSERT_EQ (rai::genesis_amount, winner.first);
}

TEST (votes, add_two)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (node1.store.environment, nullptr, true);
    ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send1));
    node1.conflicts.start (send1, false);
    auto votes1 (node1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 1, send1.clone ());
    votes1->vote (vote1);
    rai::keypair key2;
    rai::send_block send2 (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::vote vote2 (key2.pub, key2.prv, 1, send2.clone ());
    votes1->vote (vote2);
    ASSERT_EQ (3, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send1, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (key2.pub));
    ASSERT_EQ (send2, *votes1->votes.rep_votes [key2.pub].second);
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (send1, *winner.second);
}

// Higher sequence numbers change the vote
TEST (votes, add_existing)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
    rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (node1.store.environment, nullptr, true);
    ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send1));
    node1.conflicts.start (send1, false);
    auto votes1 (node1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 1, send1.clone ());
    votes1->vote (vote1);
    rai::keypair key2;
    rai::send_block send2 (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::vote vote2 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 2, send2.clone ());
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send2, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (send2, *winner.second);
}

// Lower sequence numbers are ignored
TEST (votes, add_old)
{
    rai::system system (24000, 1);
    auto & node1 (*system.nodes [0]);
	rai::genesis genesis;
    rai::keypair key1;
    rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (node1.store.environment, nullptr, true);
    ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, send1));
    node1.conflicts.start (send1, false);
    auto votes1 (node1.conflicts.roots.find (send1.root ())->second);
    rai::vote vote1 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 2, send1.clone ());
    votes1->vote (vote1);
    rai::keypair key2;
    rai::send_block send2 (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    rai::vote vote2 (rai::test_genesis_key.pub, rai::test_genesis_key.prv, 1, send2.clone ());
    votes1->vote (vote2);
    ASSERT_EQ (2, votes1->votes.rep_votes.size ());
    ASSERT_NE (votes1->votes.rep_votes.end (), votes1->votes.rep_votes.find (rai::test_genesis_key.pub));
    ASSERT_EQ (send1, *votes1->votes.rep_votes [rai::test_genesis_key.pub].second);
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (send1, *winner.second);
}

// Query for block successor
TEST (ledger, successor)
{
    rai::system system (24000, 1);
	rai::keypair key1;
	rai::genesis genesis;
	rai::send_block send1 (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
	ASSERT_EQ (rai::process_result::progress, system.nodes [0]->ledger.process (transaction, send1));
	ASSERT_EQ (send1, *system.nodes [0]->ledger.successor (genesis.hash ()));
}

TEST (fork, publish)
{
    std::weak_ptr <rai::node> node0;
    {
        rai::system system (24000, 1);
        node0 = system.nodes [0];
        auto & node1 (*system.nodes [0]);
        system.wallet (0)->store.insert (rai::test_genesis_key.prv);
        rai::keypair key1;
		rai::genesis genesis;
        std::unique_ptr <rai::send_block> send1 (new rai::send_block (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
        rai::publish publish1;
        publish1.block = std::move (send1);
        rai::keypair key2;
        std::unique_ptr <rai::send_block> send2 (new rai::send_block (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0));
        rai::publish publish2;
        publish2.block = std::move (send2);
        node1.process_message (publish1, node1.network.endpoint ());
        ASSERT_EQ (0, node1.conflicts.roots.size ());
        node1.process_message (publish2, node1.network.endpoint ());
        ASSERT_EQ (1, node1.conflicts.roots.size ());
        auto conflict1 (node1.conflicts.roots.find (publish1.block->root ()));
        ASSERT_NE (node1.conflicts.roots.end (), conflict1);
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
        auto winner (node1.ledger.winner (votes1->votes));
        ASSERT_EQ (*publish1.block, *winner.second);
        ASSERT_EQ (rai::genesis_amount, winner.first);
    }
    ASSERT_TRUE (node0.expired ());
}

TEST (ledger, fork_keep)
{
    rai::system system (24000, 2);
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	ASSERT_EQ (1, node1.peers.size ());
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish2;
    publish2.block = std::move (send2);
    node1.process_message (publish1, node1.network.endpoint ());
	node2.process_message (publish1, node2.network.endpoint ());
    ASSERT_EQ (0, node1.conflicts.roots.size ());
    ASSERT_EQ (0, node2.conflicts.roots.size ());
    node1.process_message (publish2, node1.network.endpoint ());
	node2.process_message (publish2, node2.network.endpoint ());
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    ASSERT_EQ (1, node2.conflicts.roots.size ());
    auto conflict (node2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (node2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	rai::transaction transaction (node1.store.environment, nullptr, false);
	ASSERT_TRUE (system.nodes [0]->store.block_exists (transaction, publish1.block->hash ()));
	ASSERT_TRUE (system.nodes [1]->store.block_exists (transaction, publish1.block->hash ()));
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
	}
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (rai::genesis_amount, winner.first);
	ASSERT_TRUE (system.nodes [0]->store.block_exists (transaction, publish1.block->hash ()));
	ASSERT_TRUE (system.nodes [1]->store.block_exists (transaction, publish1.block->hash ()));
}

TEST (ledger, fork_flip)
{
    rai::system system (24000, 2);
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
    ASSERT_EQ (1, node1.peers.size ());
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish2;
    publish2.block = std::move (send2);
    node1.process_message (publish1, node1.network.endpoint ());
    node2.process_message (publish2, node1.network.endpoint ());
    ASSERT_EQ (0, node1.conflicts.roots.size ());
    ASSERT_EQ (0, node2.conflicts.roots.size ());
    node1.process_message (publish2, node1.network.endpoint ());
    node2.process_message (publish1, node2.network.endpoint ());
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    ASSERT_EQ (1, node2.conflicts.roots.size ());
    auto conflict (node2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (node2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	rai::transaction transaction (node1.store.environment, nullptr, false);
    ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
    ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (rai::genesis_amount, winner.first);
    ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
    ASSERT_TRUE (node2.store.block_exists (transaction, publish1.block->hash ()));
    ASSERT_FALSE (node2.store.block_exists (transaction, publish2.block->hash ()));
}

TEST (ledger, fork_multi_flip)
{
    rai::system system (24000, 2);
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	ASSERT_EQ (1, node1.peers.size ());
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
	rai::genesis genesis;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block (key1.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish1;
    publish1.block = std::move (send1);
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block (key2.pub, genesis.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (genesis.hash ())));
    rai::publish publish2;
    publish2.block = std::move (send2);
    std::unique_ptr <rai::send_block> send3 (new rai::send_block (key2.pub, publish2.block->hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (publish2.block->hash ())));
    rai::publish publish3;
    publish3.block = std::move (send3);
    node1.process_message (publish1, node1.network.endpoint ());
	node2.process_message (publish2, node2.network.endpoint ());
    node2.process_message (publish3, node2.network.endpoint ());
    ASSERT_EQ (0, node1.conflicts.roots.size ());
    ASSERT_EQ (0, node2.conflicts.roots.size ());
    node1.process_message (publish2, node1.network.endpoint ());
    node1.process_message (publish3, node1.network.endpoint ());
	node2.process_message (publish1, node2.network.endpoint ());
    ASSERT_EQ (1, node1.conflicts.roots.size ());
    ASSERT_EQ (1, node2.conflicts.roots.size ());
    auto conflict (node2.conflicts.roots.find (genesis.hash ()));
    ASSERT_NE (node2.conflicts.roots.end (), conflict);
    auto votes1 (conflict->second);
    ASSERT_NE (nullptr, votes1);
    ASSERT_EQ (1, votes1->votes.rep_votes.size ());
	rai::transaction transaction (node1.store.environment, nullptr, false);
	ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
    ASSERT_TRUE (node2.store.block_exists (transaction, publish3.block->hash ()));
    auto iterations (0);
    while (votes1->votes.rep_votes.size () == 1)
	{
		system.service->poll_one ();
		system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
	}
    auto winner (node1.ledger.winner (votes1->votes));
    ASSERT_EQ (*publish1.block, *winner.second);
    ASSERT_EQ (rai::genesis_amount, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction, publish2.block->hash ()));
    ASSERT_FALSE (node2.store.block_exists (transaction, publish3.block->hash ()));
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
TEST (ledger, fork_bootstrap_flip)
{
    rai::system system (24000, 2);
    auto & node1 (*system.nodes [0]);
    auto & node2 (*system.nodes [1]);
	system.wallet (0)->store.insert (rai::test_genesis_key.prv);
	auto latest (system.nodes [0]->ledger.latest (rai::test_genesis_key.pub));
    rai::keypair key1;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block (key1.pub, latest, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (latest)));
    rai::keypair key2;
    std::unique_ptr <rai::send_block> send2 (new rai::send_block (key2.pub, latest, rai::genesis_amount - 100, rai::test_genesis_key.prv, rai::test_genesis_key.pub, rai::work_generate (latest)));
	rai::transaction transaction (node1.store.environment, nullptr, true);
	ASSERT_EQ (rai::process_result::progress, node1.ledger.process (transaction, *send1));
	ASSERT_EQ (rai::process_result::progress, node2.ledger.process (transaction, *send2));
	system.wallet (0)->send (rai::test_genesis_key.pub, key1.pub, 100);
	auto iterations2 (0);
	while (!node2.store.block_exists (transaction, send1->hash ()))
	{
		system.service->poll_one ();
		system.processor.poll_one ();
		++iterations2;
		ASSERT_LT (iterations2, 200);
	}
}

TEST (ledger, fail_change_old)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::change_block block (key1.pub, genesis.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::progress, result1);
    auto result2 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::old, result2);
}

TEST (ledger, fail_change_gap_previous)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::change_block block (key1.pub, 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::gap_previous, result1);
}

TEST (ledger, fail_change_bad_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::change_block block (key1.pub, genesis.hash (), rai::private_key (0), rai::public_key (0), 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::bad_signature, result1);
}

TEST (ledger, fail_change_fork)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::change_block block1 (key1.pub, genesis.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::keypair key2;
    rai::change_block block2 (key2.pub, genesis.hash (), rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::fork, result2);
}

TEST (ledger, fail_send_old)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::progress, result1);
    auto result2 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::old, result2);
}

TEST (ledger, fail_send_gap_previous)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block (key1.pub, 1, 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::gap_previous, result1);
}

TEST (ledger, fail_send_bad_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block (key1.pub, genesis.hash (), 1, 0, 0, 0);
    auto result1 (ledger.process (transaction, block));
    ASSERT_EQ (rai::process_result::bad_signature, result1);
}

TEST (ledger, fail_send_overspend)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::keypair key2;
    rai::send_block block2 (key2.pub, block1.hash (), 2, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::overspend, ledger.process (transaction, block2));
}

TEST (ledger, fail_send_fork)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::keypair key2;
    rai::send_block block2 (key2.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::fork, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_old)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::open_block block2 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::old, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_gap_source)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::open_block block2 (key1.pub, 0, 1, key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::gap_source, result2);
}

TEST (ledger, fail_open_overreceive)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::open_block block2 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    rai::open_block block3 (key1.pub, 1, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::unreceivable, ledger.process (transaction, block3));
}

TEST (ledger, fail_open_bad_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::open_block block2 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    block2.signature.clear ();
    ASSERT_EQ (rai::process_result::bad_signature, ledger.process (transaction, block2));
}

TEST (ledger, fail_open_fork_previous)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block3));
    rai::open_block block4 (key1.pub, 0, block2.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::fork, ledger.process (transaction, block4));
}

TEST (ledger, fail_open_account_mismatch)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::open_block block2 (1, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (1));
    ASSERT_EQ (rai::process_result::account_mismatch, ledger.process (transaction, block2));
}

TEST (ledger, fail_receive_old)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block1));
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block2));
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block3));
    rai::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, block4));
    ASSERT_EQ (rai::process_result::old, ledger.process (transaction, block4));
}

TEST (ledger, fail_receive_gap_source)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result3 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, 0);
    auto result4 (ledger.process (transaction, block4));
    ASSERT_EQ (rai::process_result::gap_source, result4);
}

TEST (ledger, fail_receive_overreceive)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::open_block block2 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result3 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, 0);
    auto result4 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::unreceivable, result4);
}

TEST (ledger, fail_receive_bad_signature)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result3 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4 (block3.hash (), block2.hash (), 0, 0, 0);
    auto result4 (ledger.process (transaction, block4));
    ASSERT_EQ (rai::process_result::bad_signature, result4);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result3 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, 0);
    auto result4 (ledger.process (transaction, block4));
    ASSERT_EQ (rai::process_result::gap_previous, result4);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, 0);
    auto result3 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::gap_previous, result3);
}

TEST (ledger, fail_receive_fork_previous)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key1;
    rai::send_block block1 (key1.pub, genesis.hash (), 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result1 (ledger.process (transaction, block1));
    ASSERT_EQ (rai::process_result::progress, result1);
    rai::send_block block2 (key1.pub, block1.hash (), 0, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    auto result2 (ledger.process (transaction, block2));
    ASSERT_EQ (rai::process_result::progress, result2);
    rai::open_block block3 (key1.pub, 0, block1.hash (), key1.prv, key1.pub, rai::work_generate (key1.pub));
    auto result3 (ledger.process (transaction, block3));
    ASSERT_EQ (rai::process_result::progress, result3);
    rai::keypair key2;
    rai::send_block block4 (key1.pub, block3.hash (), 1, key1.prv, key1.pub, 0);
    auto result4 (ledger.process (transaction, block4));
    ASSERT_EQ (rai::process_result::progress, result4);
    rai::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, 0);
    auto result5 (ledger.process (transaction, block5));
    ASSERT_EQ (rai::process_result::fork, result5);
}

TEST (ledger, latest_empty)
{
	bool init;
	rai::block_store store (init, rai::unique_path ());
	ASSERT_TRUE (!init);
	rai::ledger ledger (store);
	rai::keypair key;
	auto latest (ledger.latest (key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
    bool init;
    rai::block_store store (init, rai::unique_path ());
    ASSERT_TRUE (!init);
    rai::ledger ledger (store);
    rai::genesis genesis;
	rai::transaction transaction (store.environment, nullptr, true);
    genesis.initialize (transaction, store);
    rai::keypair key;
    ASSERT_EQ (key.pub, ledger.latest_root (key.pub));
    auto hash1 (ledger.latest (rai::test_genesis_key.pub));
    rai::send_block send (0, hash1, 1, rai::test_genesis_key.prv, rai::test_genesis_key.pub, 0);
    ASSERT_EQ (rai::process_result::progress, ledger.process (transaction, send));
    ASSERT_EQ (send.hash (), ledger.latest_root (rai::test_genesis_key.pub));
}