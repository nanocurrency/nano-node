#include <gtest/gtest.h>

#include <rai/node.hpp>
#include <fstream>

TEST (wallet, no_key)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    rai::private_key prv1;
    ASSERT_TRUE (wallet.fetch (key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password ());
}

TEST (wallet, retrieval)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_TRUE (wallet.valid_password ());
    wallet.insert (key1.prv);
    rai::private_key prv1;
    ASSERT_FALSE (wallet.fetch (key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password ());
    ASSERT_EQ (key1.prv, prv1);
    wallet.password.values [0]->bytes [16] ^= 1;
    rai::private_key prv2;
    ASSERT_TRUE (wallet.fetch (key1.pub, prv2));
    ASSERT_FALSE (wallet.valid_password ());
}

TEST (wallet, empty_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, false);
    auto i (wallet.begin (transaction));
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (key1.prv);
	rai::transaction transaction (environment, nullptr, false);
    for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
    {
        ASSERT_EQ (key1.pub, i->first);
        ASSERT_EQ (key1.prv, rai::uint256_union (i->second).prv (wallet.wallet_key (), wallet.salt ().owords [0]));
    }
}

TEST (wallet, two_item_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    rai::keypair key2;
    wallet.insert (key1.prv);
    wallet.insert (key2.prv);
    std::unordered_set <rai::public_key> pubs;
    std::unordered_set <rai::private_key> prvs;
	rai::transaction transaction (environment, nullptr, false);
    for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
    {
        pubs.insert (i->first);
        prvs.insert (rai::uint256_union (i->second).prv (wallet.wallet_key (), wallet.salt ().owords [0]));
    }
    ASSERT_EQ (2, pubs.size ());
    ASSERT_EQ (2, prvs.size ());
    ASSERT_NE (pubs.end (), pubs.find (key1.pub));
    ASSERT_NE (prvs.end (), prvs.find (key1.prv));
    ASSERT_NE (pubs.end (), pubs.find (key2.pub));
    ASSERT_NE (prvs.end (), prvs.find (key2.prv));
}

TEST (wallet, insufficient_spend)
{
    rai::system system (24000, 1);
    rai::keypair key1;
    ASSERT_TRUE (system.wallet (0)->send_all (key1.pub, 500));
}

TEST (wallet, spend_all_one)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier1);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, std::numeric_limits <rai::uint128_t>::max ()));
    rai::frontier frontier2;
    system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier2);
    ASSERT_NE (frontier1, frontier2);
    auto block (system.nodes [0]->store.block_get (transaction, frontier2.hash));
    ASSERT_NE (nullptr, block);
    ASSERT_EQ (frontier1.hash, block->previous ());
    ASSERT_TRUE (frontier2.balance.is_zero ());
    ASSERT_EQ (0, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
}

TEST (wallet, spend)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier1);
    rai::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_TRUE (system.wallet (0)->send (0, key2.pub, 0));
    ASSERT_FALSE (system.wallet (0)->send (rai::test_genesis_key.pub, key2.pub, std::numeric_limits <rai::uint128_t>::max ()));
    rai::frontier frontier2;
    system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier2);
    ASSERT_NE (frontier1, frontier2);
    auto block (system.nodes [0]->store.block_get (transaction, frontier2.hash));
    ASSERT_NE (nullptr, block);
    ASSERT_EQ (frontier1.hash, block->previous ());
    ASSERT_TRUE (frontier2.balance.is_zero ());
    ASSERT_EQ (0, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
}

TEST (wallet, DISABLED_two_spend)
{/*
    rai::keypair key1;
    rai::keypair key2;
    rai::uint256_union password;
    rai::wallet_store wallet (0, rai::wallet_store_temp);
    wallet.insert (key1.pub, key1.prv, password);
    wallet.insert (key2.pub, key2.prv, password);
    rai::block_store store (rai::block_store_temp);
    rai::ledger ledger (store);
    rai::genesis genesis1 (key1.pub, 100);
    genesis1.initialize (store);
    rai::frontier frontier1;
    ASSERT_FALSE (store.latest_get (key1.pub, frontier1));
    rai::genesis genesis2 (key2.pub, 400);
    genesis2.initialize (store);
    rai::frontier frontier2;
    ASSERT_FALSE (store.latest_get (key2.pub, frontier2));
    rai::keypair key3;
    std::vector <std::unique_ptr <rai::send_block>> blocks;
    ASSERT_FALSE (wallet.generate_send (ledger, key3.pub, 500, password, blocks));
    ASSERT_EQ (2, blocks.size ());
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [] (std::unique_ptr <rai::send_block> const & block) {return block->hashables.balance == 0;}));
    ASSERT_TRUE (std::all_of (blocks.begin (), blocks.end (), [key3] (std::unique_ptr <rai::send_block> const & block) {return block->hashables.destination == key3.pub;}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key1] (std::unique_ptr <rai::send_block> const & block) {return !rai::validate_message(key1.pub, block->hash (), block->signature);}));
    ASSERT_TRUE (std::any_of (blocks.begin (), blocks.end (), [key2] (std::unique_ptr <rai::send_block> const & block) {return !rai::validate_message(key2.pub, block->hash (), block->signature);}));*/
}

TEST (wallet, partial_spend)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::frontier frontier1;
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    ASSERT_FALSE (system.nodes [0]->store.latest_get (transaction, rai::test_genesis_key.pub, frontier1));
    for (auto i (0); i < 50; ++i)
    {
        rai::keypair key;
        system.wallet (0)->store.insert (key.prv);
    }
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_all (key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.nodes [0]->ledger.account_balance(transaction, rai::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::uint256_union account;
    ASSERT_EQ (wallet.end (), wallet.find (account));
}

TEST (wallet, find_existing)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_FALSE (wallet.exists (key1.pub));
    wallet.insert (key1.prv);
    ASSERT_TRUE (wallet.exists (key1.pub));
    auto existing (wallet.find (key1.pub));
    ASSERT_NE (wallet.end (), existing);
    ++existing;
    ASSERT_EQ (wallet.end (), existing);
}

TEST (wallet, rekey)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
	ASSERT_FALSE (init);
    ASSERT_TRUE (wallet.password.value ().is_zero ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (key1.prv);
    rai::uint256_union prv1;
    wallet.fetch (key1.pub, prv1);
    ASSERT_EQ (key1.prv, prv1);
    ASSERT_FALSE (wallet.rekey ("1"));
    ASSERT_EQ (wallet.derive_key ("1"), wallet.password.value ());
    rai::uint256_union prv2;
    wallet.fetch (key1.pub, prv2);
    ASSERT_EQ (key1.prv, prv2);
    *wallet.password.values [0] = 2;
    ASSERT_TRUE (wallet.rekey ("2"));
}

TEST (base58, encode_zero)
{
    rai::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    rai::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_all)
{
    rai::uint256_union number0;
    number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    std::string str0;
    number0.encode_base58check (str0);
    ASSERT_EQ (50, str0.size ());
    rai::uint256_union number1;
    ASSERT_FALSE (number1.decode_base58check (str0));
    ASSERT_EQ (number0, number1);
}

TEST (base58, encode_fail)
{
    rai::uint256_union number0 (0);
    std::string str0;
    number0.encode_base58check (str0);
    str0 [16] ^= 1;
    rai::uint256_union number1;
    ASSERT_TRUE (number1.decode_base58check (str0));
}

TEST (wallet, hash_password)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    rai::wallet_store wallet (init, environment, "0");
    ASSERT_FALSE (init);
    auto hash1 (wallet.derive_key (""));
    auto hash2 (wallet.derive_key (""));
    ASSERT_EQ (hash1, hash2);
    auto hash3 (wallet.derive_key ("a"));
    ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
    rai::uint256_union value0;
    rai::fan fan (value0, 1024);
    for (auto & i: fan.values)
    {
        ASSERT_NE (value0, *i);
    }
    auto value1 (fan.value ());
    ASSERT_EQ (value0, value1);
}

TEST (fan, change)
{
    rai::uint256_union value0;
    rai::uint256_union value1;
    ASSERT_NE (value0, value1);
    rai::fan fan (value0, 1024);
    ASSERT_EQ (value0, fan.value ());
    fan.value_set (value1);
    ASSERT_EQ (value1, fan.value ());
}

TEST (wallet, reopen_default_password)
{
	bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
    {
        rai::wallet_store wallet (init, environment, "0");
        ASSERT_FALSE (init);
		wallet.rekey ("");
        ASSERT_TRUE (wallet.valid_password ());
    }
    {
        bool init;
        rai::wallet_store wallet (init, environment, "0");
        ASSERT_FALSE (init);
        ASSERT_TRUE (wallet.valid_password ());
        wallet.enter_password (" ");
        ASSERT_FALSE (wallet.valid_password ());
        wallet.enter_password ("");
        ASSERT_TRUE (wallet.valid_password ());
    }
}

TEST (wallet, representative)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
    rai::wallet_store wallet (error, environment, "0");
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet.is_representative ());
    ASSERT_EQ (rai::genesis_account, wallet.representative ());
    ASSERT_FALSE (wallet.is_representative ());
    rai::keypair key;
    wallet.representative_set (key.pub);
    ASSERT_FALSE (wallet.is_representative ());
    ASSERT_EQ (key.pub, wallet.representative());
    ASSERT_FALSE (wallet.is_representative ());
    wallet.insert (key.prv);
    ASSERT_TRUE (wallet.is_representative ());
}

TEST (wallet, serialize_json_empty)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
    rai::wallet_store wallet1 (error, environment, "0");
	ASSERT_FALSE (error);
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, environment, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
	rai::transaction transaction (environment, nullptr, false);
    ASSERT_EQ (wallet1.end (), wallet1.begin (transaction));
    ASSERT_EQ (wallet2.end (), wallet2.begin (transaction));
}

TEST (wallet, serialize_json_one)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
    rai::wallet_store wallet1 (error, environment, "0");
	ASSERT_FALSE (error);
    rai::keypair key;
    wallet1.insert (key.prv);
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, environment, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
    ASSERT_TRUE (wallet2.exists (key.pub));
    rai::private_key prv;
    wallet2.fetch (key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
    rai::wallet_store wallet1 (error, environment, "0");
	ASSERT_FALSE (error);
    rai::keypair key;
    wallet1.rekey ("password");
    wallet1.insert (key.prv);
    std::string serialized;
    wallet1.serialize_json (serialized);
    rai::wallet_store wallet2 (error, environment, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet2.valid_password ());
    wallet2.enter_password ("password");
    ASSERT_TRUE (wallet2.valid_password ());
    ASSERT_EQ (wallet1.wallet_key (), wallet2.wallet_key ());
    ASSERT_EQ (wallet1.salt (), wallet2.salt ());
    ASSERT_EQ (wallet1.check (), wallet2.check ());
    ASSERT_EQ (wallet1.representative (), wallet2.representative ());
    ASSERT_TRUE (wallet2.exists (key.pub));
    rai::private_key prv;
    wallet2.fetch (key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
    rai::wallet_store wallet1 (error, environment, "0");
    ASSERT_FALSE (error);
    rai::keypair key1;
    wallet1.insert (key1.prv);
    rai::wallet_store wallet2 (error, environment, "1");
    ASSERT_FALSE (error);
    rai::keypair key2;
    wallet2.insert (key2.prv);
    ASSERT_FALSE (wallet1.exists (key2.pub));
    ASSERT_TRUE (wallet2.exists (key2.pub));
    std::vector <rai::public_key> keys;
    keys.push_back (key2.pub);
    ASSERT_FALSE (wallet1.move (wallet2, keys));
    ASSERT_TRUE (wallet1.exists (key2.pub));
    ASSERT_FALSE (wallet2.exists (key2.pub));
}

TEST (wallet, work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
    wallet->store.insert (rai::test_genesis_key.prv);
    auto account1 (system.account (0));
    uint64_t work1;
	ASSERT_TRUE (wallet->store.work_get (1000, work1));
    ASSERT_FALSE (wallet->store.work_get (account1, work1));
	ASSERT_EQ (0, work1);
    ASSERT_TRUE (wallet->store.exists (account1));
    wallet->work_update (account1, 0, rai::work_generate (0));
    {
        std::lock_guard <std::mutex> lock (wallet->mutex);
        ASSERT_FALSE (rai::work_validate (0, wallet->work_fetch(account1, 0)));
    }
    ASSERT_FALSE (wallet->store.work_get (account1, work1));
	ASSERT_EQ (0, work1);
    auto root1 (system.nodes [0]->ledger.latest_root (account1));
    auto work2 (rai::work_generate (root1));
    wallet->work_update (account1, root1, work2);
    uint64_t work3;
    ASSERT_FALSE (wallet->store.work_get (account1, work3));
    {
        std::lock_guard <std::mutex> lock (wallet->mutex);
        auto work4 (wallet->work_fetch (account1, root1));
        ASSERT_FALSE (rai::work_validate (root1, work4));
        ASSERT_EQ (work3, work4);
    }
    ASSERT_EQ (work2, work3);
}

TEST (wallet, work_generate)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
    wallet->store.insert (rai::test_genesis_key.prv);
    auto account1 (system.account (0));
    uint64_t work1;
    ASSERT_FALSE (wallet->store.work_get (account1, work1));
	ASSERT_EQ (0, work1);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    auto amount1 (system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub));
    rai::keypair key;
    wallet->send_all (key.pub, 100);
    auto iterations1 (0);
    while (system.nodes [0]->ledger.account_balance (transaction, rai::test_genesis_key.pub) == amount1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    auto iterations2 (0);
    while (wallet->store.work_get (account1, work1) || rai::work_validate (system.nodes [0]->ledger.latest_root (account1), work1))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
}

TEST (wallet, startup_work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
    wallet->insert (rai::test_genesis_key.prv);
    auto account1 (system.account (0));
    uint64_t work1;
    ASSERT_FALSE (wallet->store.work_get (account1, work1));
	ASSERT_EQ (0, work1);
    auto iterations2 (0);
	while (work1 == 0)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
		ASSERT_LT (iterations2, 200);
		ASSERT_FALSE (wallet->store.work_get (account1, work1));
    }
}

TEST (wallet, unsynced_work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
	wallet->store.work_put (0, 0);
	std::lock_guard <std::mutex> lock (wallet->mutex);
	auto work1 (wallet->work_fetch (0, 0));
	ASSERT_FALSE (rai::work_validate (0, work1));
}