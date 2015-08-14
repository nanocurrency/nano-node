#include <gtest/gtest.h>

#include <rai/node.hpp>
#include <fstream>

TEST (wallet, no_key)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    rai::private_key prv1;
    ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet, retrieval)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_TRUE (wallet.valid_password (transaction));
    wallet.insert (transaction, key1.prv);
    rai::private_key prv1;
    ASSERT_FALSE (wallet.fetch (transaction, key1.pub, prv1));
    ASSERT_TRUE (wallet.valid_password (transaction));
    ASSERT_EQ (key1.prv, prv1);
    wallet.password.values [0]->bytes [16] ^= 1;
    rai::private_key prv2;
    ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv2));
    ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet, empty_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    auto i (wallet.begin (transaction));
    auto j (wallet.end ());
    ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (transaction, key1.prv);
    for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
    {
        ASSERT_EQ (key1.pub, i->first);
        ASSERT_EQ (key1.prv, rai::wallet_value (i->second).key.prv (wallet.wallet_key (transaction), wallet.salt (transaction).owords [0]));
    }
}

TEST (wallet, two_item_iteration)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::keypair key1;
	rai::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set <rai::public_key> pubs;
	std::unordered_set <rai::private_key> prvs;
	{
		rai::transaction transaction (environment, nullptr, true);
		rai::wallet_store wallet (init, transaction, "0");
		ASSERT_FALSE (init);
		wallet.insert (transaction, key1.prv);
		wallet.insert (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
		{
			pubs.insert (i->first);
			prvs.insert (rai::wallet_value (i->second).key.prv (wallet.wallet_key (transaction), wallet.salt (transaction).owords [0]));
		}
	}
    ASSERT_EQ (2, pubs.size ());
    ASSERT_EQ (2, prvs.size ());
    ASSERT_NE (pubs.end (), pubs.find (key1.pub));
    ASSERT_NE (prvs.end (), prvs.find (key1.prv));
    ASSERT_NE (pubs.end (), pubs.find (key2.pub));
    ASSERT_NE (prvs.end (), prvs.find (key2.prv));
}

TEST (wallet, insufficient_spend_one)
{
    rai::system system (24000, 1);
    rai::keypair key1;
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key1.pub, 500));
    ASSERT_TRUE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key1.pub, rai::genesis_amount));
}

TEST (wallet, spend_all_one)
{
    rai::system system (24000, 1);
    rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key2.pub, std::numeric_limits <rai::uint128_t>::max ()));
    rai::account_info info2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		system.nodes [0]->store.account_get (transaction, rai::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes [0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
    ASSERT_TRUE (info2.balance.is_zero ());
    ASSERT_EQ (0, system.nodes [0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, spend)
{
	rai::system system (24000, 1);
	rai::block_hash latest1 (system.nodes [0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_TRUE (system.wallet (0)->send_sync (0, key2.pub, 0));
    ASSERT_FALSE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key2.pub, std::numeric_limits <rai::uint128_t>::max ()));
    rai::account_info info2;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		system.nodes [0]->store.account_get (transaction, rai::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes [0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
    ASSERT_TRUE (info2.balance.is_zero ());
    ASSERT_EQ (0, system.nodes [0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, change)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
	ASSERT_EQ (rai::genesis_account, system.nodes [0]->representative (rai::test_genesis_key.pub));
	ASSERT_FALSE (system.wallet (0)->change_sync (rai::test_genesis_key.pub, key2.pub));
	ASSERT_EQ (key2.pub, system.nodes [0]->representative (rai::test_genesis_key.pub));
}

TEST (wallet, partial_spend)
{
    rai::system system (24000, 1);
	system.wallet (0)->insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.nodes [0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
    rai::system system (24000, 1);
	{
		system.wallet (0)->insert (rai::test_genesis_key.prv);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		rai::account_info info1;
		ASSERT_FALSE (system.nodes [0]->store.account_get (transaction, rai::test_genesis_key.pub, info1));
		for (auto i (0); i < 50; ++i)
		{
			rai::keypair key;
			system.wallet (0)->insert (key.prv);
		}
	}
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send_sync (rai::test_genesis_key.pub, key2.pub, 500));
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max () - 500, system.nodes [0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    rai::uint256_union account (0);
    ASSERT_EQ (wallet.end (), wallet.find (transaction, account));
}

TEST (wallet, find_existing)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    rai::keypair key1;
    ASSERT_FALSE (wallet.exists (transaction, key1.pub));
    wallet.insert (transaction, key1.prv);
    ASSERT_TRUE (wallet.exists (transaction, key1.pub));
    auto existing (wallet.find (transaction, key1.pub));
    ASSERT_NE (wallet.end (), existing);
    ++existing;
    ASSERT_EQ (wallet.end (), existing);
}

TEST (wallet, rekey)
{
    bool init;
	rai::mdb_env environment (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
	ASSERT_FALSE (init);
    ASSERT_TRUE (wallet.password.value ().is_zero ());
    ASSERT_FALSE (init);
    rai::keypair key1;
    wallet.insert (transaction, key1.prv);
    rai::uint256_union prv1;
    wallet.fetch (transaction, key1.pub, prv1);
    ASSERT_EQ (key1.prv, prv1);
    ASSERT_FALSE (wallet.rekey (transaction, "1"));
    ASSERT_EQ (wallet.derive_key (transaction, "1"), wallet.password.value ());
    rai::uint256_union prv2;
    wallet.fetch (transaction, key1.pub, prv2);
    ASSERT_EQ (key1.prv, prv2);
    *wallet.password.values [0] = 2;
    ASSERT_TRUE (wallet.rekey (transaction, "2"));
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
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (init, transaction, "0");
    ASSERT_FALSE (init);
    auto hash1 (wallet.derive_key (transaction, ""));
    auto hash2 (wallet.derive_key (transaction, ""));
    ASSERT_EQ (hash1, hash2);
    auto hash3 (wallet.derive_key (transaction, "a"));
    ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
    rai::uint256_union value0 (0);
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
    rai::uint256_union value0 (0);
    rai::uint256_union value1 (1);
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
	rai::transaction transaction (environment, nullptr, true);
	ASSERT_FALSE (init);
    {
        rai::wallet_store wallet (init, transaction, "0");
        ASSERT_FALSE (init);
		wallet.rekey (transaction, "");
        ASSERT_TRUE (wallet.valid_password (transaction));
    }
    {
        bool init;
        rai::wallet_store wallet (init, transaction, "0");
        ASSERT_FALSE (init);
        ASSERT_TRUE (wallet.valid_password (transaction));
        wallet.enter_password (transaction, " ");
        ASSERT_FALSE (wallet.valid_password (transaction));
        wallet.enter_password (transaction, "");
        ASSERT_TRUE (wallet.valid_password (transaction));
    }
}

TEST (wallet, representative)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet (error, transaction, "0");
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet.is_representative (transaction));
    ASSERT_EQ (rai::genesis_account, wallet.representative (transaction));
    ASSERT_FALSE (wallet.is_representative (transaction));
    rai::keypair key;
    wallet.representative_set (transaction, key.pub);
    ASSERT_FALSE (wallet.is_representative (transaction));
    ASSERT_EQ (key.pub, wallet.representative (transaction));
    ASSERT_FALSE (wallet.is_representative (transaction));
    wallet.insert (transaction, key.prv);
    ASSERT_TRUE (wallet.is_representative (transaction));
}

TEST (wallet, serialize_json_empty)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet1 (error, transaction, "0");
	ASSERT_FALSE (error);
    std::string serialized;
    wallet1.serialize_json (transaction, serialized);
    rai::wallet_store wallet2 (error, transaction, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (transaction), wallet2.wallet_key (transaction));
    ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
    ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
    ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
    ASSERT_EQ (wallet1.end (), wallet1.begin (transaction));
    ASSERT_EQ (wallet2.end (), wallet2.begin (transaction));
}

TEST (wallet, serialize_json_one)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet1 (error, transaction, "0");
	ASSERT_FALSE (error);
    rai::keypair key;
    wallet1.insert (transaction, key.prv);
    std::string serialized;
    wallet1.serialize_json (transaction, serialized);
    rai::wallet_store wallet2 (error, transaction, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_EQ (wallet1.wallet_key (transaction), wallet2.wallet_key (transaction));
    ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
    ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
    ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
    ASSERT_TRUE (wallet2.exists (transaction, key.pub));
    rai::private_key prv;
    wallet2.fetch (transaction, key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet1 (error, transaction, "0");
	ASSERT_FALSE (error);
    rai::keypair key;
    wallet1.rekey (transaction, "password");
    wallet1.insert (transaction, key.prv);
    std::string serialized;
    wallet1.serialize_json (transaction, serialized);
    rai::wallet_store wallet2 (error, transaction, "1", serialized);
    ASSERT_FALSE (error);
    ASSERT_FALSE (wallet2.valid_password (transaction));
    wallet2.enter_password (transaction, "password");
    ASSERT_TRUE (wallet2.valid_password (transaction));
    ASSERT_EQ (wallet1.wallet_key (transaction), wallet2.wallet_key (transaction));
    ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
    ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
    ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
    ASSERT_TRUE (wallet2.exists (transaction, key.pub));
    rai::private_key prv;
    wallet2.fetch (transaction, key.pub, prv);
    ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
    auto error (false);
	rai::mdb_env environment (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (environment, nullptr, true);
    rai::wallet_store wallet1 (error, transaction, "0");
    ASSERT_FALSE (error);
    rai::keypair key1;
    wallet1.insert (transaction, key1.prv);
    rai::wallet_store wallet2 (error, transaction, "1");
    ASSERT_FALSE (error);
    rai::keypair key2;
    wallet2.insert (transaction, key2.prv);
    ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
    ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
    std::vector <rai::public_key> keys;
    keys.push_back (key2.pub);
    ASSERT_FALSE (wallet1.move (transaction, wallet2, keys));
    ASSERT_TRUE (wallet1.exists (transaction, key2.pub));
    ASSERT_FALSE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, import)
{
    rai::system system (24000, 2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	rai::keypair key1;
	wallet1->insert (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, ""));
	ASSERT_FALSE (error);
	ASSERT_TRUE (wallet2->exists (key1.pub));
}

TEST (wallet_store, fail_import_bad_password)
{
    rai::system system (24000, 2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	rai::keypair key1;
	wallet1->insert (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

// Test arbitrary work can be generated
TEST (wallet, empty_work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
	ASSERT_FALSE (rai::work_validate (1, wallet->work_fetch (transaction, 0, 1)));
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
    wallet->insert (rai::test_genesis_key.prv);
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
    auto account1 (system.account (transaction, 0));
    auto root1 (system.nodes [0]->ledger.latest_root (transaction, account1));
    uint64_t work3;
	// Make sure work_get and work_fetch retrieve the same thing
    ASSERT_FALSE (wallet->store.work_get (transaction, account1, work3));
	auto work4 (wallet->work_fetch (transaction, account1, root1));
	ASSERT_FALSE (rai::work_validate (root1, work4));
	ASSERT_EQ (work3, work4);
}

TEST (wallet, work_generate)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
	rai::uint128_t amount1 (system.nodes [0]->balance (rai::test_genesis_key.pub));
	uint64_t work1;
	wallet->insert (rai::test_genesis_key.prv);
	rai::account account1;
	{
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		account1 = system.account (transaction, 0);
	}
	rai::keypair key;
    wallet->send_sync (rai::test_genesis_key.pub, key.pub, 100);
    auto iterations1 (0);
    while (system.nodes [0]->ledger.account_balance (rai::transaction (system.nodes [0]->store.environment, nullptr, false), rai::test_genesis_key.pub) == amount1)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    auto iterations2 (0);
	auto again (true);
    while (again)
    {
		system.poll ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
		rai::transaction transaction (system.nodes [0]->store.environment, nullptr, false);
		again = wallet->store.work_get (transaction, account1, work1) || rai::work_validate (system.nodes [0]->ledger.latest_root (transaction, account1), work1);
    }
}

TEST (wallet, unsynced_work)
{
    rai::system system (24000, 1);
    auto wallet (system.wallet (0));
	rai::transaction transaction (system.nodes [0]->store.environment, nullptr, true);
	wallet->store.work_put (transaction, 0, 0);
	auto work1 (wallet->work_fetch (transaction, 0, 1));
	ASSERT_FALSE (rai::work_validate (1, work1));
}
