#include <gtest/gtest.h>

#include <fstream>
#include <rai/core_test/testutil.hpp>
#include <rai/node/testing.hpp>

using namespace std::chrono_literals;

TEST (wallet, no_key)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::keypair key1;
	rai::raw_key prv1;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet, fetch_locked)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_TRUE (wallet.valid_password (transaction));
	rai::keypair key1;
	ASSERT_EQ (key1.pub, wallet.insert_adhoc (transaction, key1.prv));
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_FALSE (key2.is_zero ());
	rai::raw_key key3;
	key3.data = 1;
	wallet.password.value_set (key3);
	ASSERT_FALSE (wallet.valid_password (transaction));
	rai::raw_key key4;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, key4));
	ASSERT_TRUE (wallet.fetch (transaction, key2, key4));
}

TEST (wallet, retrieval)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::keypair key1;
	ASSERT_TRUE (wallet.valid_password (transaction));
	wallet.insert_adhoc (transaction, key1.prv);
	rai::raw_key prv1;
	ASSERT_FALSE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
	ASSERT_EQ (key1.prv, prv1);
	wallet.password.values[0]->bytes[16] ^= 1;
	rai::raw_key prv2;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv2));
	ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet, empty_iteration)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	auto i (wallet.begin (transaction));
	auto j (wallet.end ());
	ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
	{
		ASSERT_EQ (key1.pub, rai::uint256_union (i->first));
		rai::raw_key password;
		wallet.wallet_key (password, transaction);
		rai::raw_key key;
		key.decrypt (rai::wallet_value (i->second).key, password, (rai::uint256_union (i->first)).owords[0].number ());
		ASSERT_EQ (key1.prv, key);
	}
}

TEST (wallet, two_item_iteration)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::keypair key1;
	rai::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set<rai::public_key> pubs;
	std::unordered_set<rai::private_key> prvs;
	rai::kdf kdf;
	{
		rai::transaction transaction (env.tx_begin (true));
		rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.insert_adhoc (transaction, key1.prv);
		wallet.insert_adhoc (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
		{
			pubs.insert (rai::uint256_union (i->first));
			rai::raw_key password;
			wallet.wallet_key (password, transaction);
			rai::raw_key key;
			key.decrypt (rai::wallet_value (i->second).key, password, (rai::uint256_union (i->first)).owords[0].number ());
			prvs.insert (key.data);
		}
	}
	ASSERT_EQ (2, pubs.size ());
	ASSERT_EQ (2, prvs.size ());
	ASSERT_NE (pubs.end (), pubs.find (key1.pub));
	ASSERT_NE (prvs.end (), prvs.find (key1.prv.data));
	ASSERT_NE (pubs.end (), pubs.find (key2.pub));
	ASSERT_NE (prvs.end (), prvs.find (key2.prv.data));
}

TEST (wallet, insufficient_spend_one)
{
	rai::system system (24000, 1);
	rai::keypair key1;
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, 500));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key1.pub, rai::genesis_amount));
}

TEST (wallet, spend_all_one)
{
	rai::system system (24000, 1);
	rai::block_hash latest1 (system.nodes[0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, std::numeric_limits<rai::uint128_t>::max ()));
	rai::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		system.nodes[0]->store.account_get (transaction, rai::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes[0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, send_async)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	boost::thread thread ([&system]() {
		system.deadline_set (10s);
		while (!system.nodes[0]->balance (rai::test_genesis_key.pub).is_zero ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	});
	bool success (false);
	system.wallet (0)->send_async (rai::test_genesis_key.pub, key2.pub, std::numeric_limits<rai::uint128_t>::max (), [&success](std::shared_ptr<rai::block> block_a) { ASSERT_NE (nullptr, block_a); success = true; });
	thread.join ();
	ASSERT_TRUE (success);
}

TEST (wallet, spend)
{
	rai::system system (24000, 1);
	rai::block_hash latest1 (system.nodes[0]->latest (rai::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (0, key2.pub, 0));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, std::numeric_limits<rai::uint128_t>::max ()));
	rai::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin ());
		system.nodes[0]->store.account_get (transaction, rai::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes[0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, change)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	auto block1 (system.nodes[0]->representative (rai::test_genesis_key.pub));
	ASSERT_FALSE (block1.is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->change_action (rai::test_genesis_key.pub, key2.pub));
	auto block2 (system.nodes[0]->representative (rai::test_genesis_key.pub));
	ASSERT_FALSE (block2.is_zero ());
	ASSERT_NE (block1, block2);
}

TEST (wallet, partial_spend)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - 500, system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
	rai::system system (24000, 1);
	{
		system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
		auto transaction (system.nodes[0]->store.tx_begin ());
		rai::account_info info1;
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, rai::test_genesis_key.pub, info1));
		for (auto i (0); i < 50; ++i)
		{
			rai::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);
		}
	}
	rai::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<rai::uint128_t>::max () - 500, system.nodes[0]->balance (rai::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::uint256_union account (1000);
	ASSERT_EQ (wallet.end (), wallet.find (transaction, account));
}

TEST (wallet, find_existing)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));
	auto existing (wallet.find (transaction, key1.pub));
	ASSERT_NE (wallet.end (), existing);
	++existing;
	ASSERT_EQ (wallet.end (), existing);
}

TEST (wallet, rekey)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::raw_key password;
	wallet.password.value (password);
	ASSERT_TRUE (password.data.is_zero ());
	ASSERT_FALSE (init);
	rai::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	rai::raw_key prv1;
	wallet.fetch (transaction, key1.pub, prv1);
	ASSERT_EQ (key1.prv, prv1);
	ASSERT_FALSE (wallet.rekey (transaction, "1"));
	wallet.password.value (password);
	rai::raw_key password1;
	wallet.derive_key (password1, transaction, "1");
	ASSERT_EQ (password1, password);
	rai::raw_key prv2;
	wallet.fetch (transaction, key1.pub, prv2);
	ASSERT_EQ (key1.prv, prv2);
	*wallet.password.values[0] = 2;
	ASSERT_TRUE (wallet.rekey (transaction, "2"));
}

TEST (account, encode_zero)
{
	rai::uint256_union number0 (0);
	std::string str0;
	number0.encode_account (str0);
	ASSERT_EQ (64, str0.size ());
	rai::uint256_union number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_all)
{
	rai::uint256_union number0;
	number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	std::string str0;
	number0.encode_account (str0);
	ASSERT_EQ (64, str0.size ());
	rai::uint256_union number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_fail)
{
	rai::uint256_union number0 (0);
	std::string str0;
	number0.encode_account (str0);
	str0[16] ^= 1;
	rai::uint256_union number1;
	ASSERT_TRUE (number1.decode_account (str0));
}

TEST (wallet, hash_password)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	rai::raw_key hash1;
	wallet.derive_key (hash1, transaction, "");
	rai::raw_key hash2;
	wallet.derive_key (hash2, transaction, "");
	ASSERT_EQ (hash1, hash2);
	rai::raw_key hash3;
	wallet.derive_key (hash3, transaction, "a");
	ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
	rai::uint256_union value0 (0);
	rai::fan fan (value0, 1024);
	for (auto & i : fan.values)
	{
		ASSERT_NE (value0, *i);
	}
	rai::raw_key value1;
	fan.value (value1);
	ASSERT_EQ (value0, value1.data);
}

TEST (fan, change)
{
	rai::raw_key value0;
	value0.data = 0;
	rai::raw_key value1;
	value1.data = 1;
	ASSERT_NE (value0, value1);
	rai::fan fan (value0.data, 1024);
	ASSERT_EQ (1024, fan.values.size ());
	rai::raw_key value2;
	fan.value (value2);
	ASSERT_EQ (value0, value2);
	fan.value_set (value1);
	fan.value (value2);
	ASSERT_EQ (value1, value2);
}

TEST (wallet, reopen_default_password)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	rai::transaction transaction (env.tx_begin (true));
	ASSERT_FALSE (init);
	rai::kdf kdf;
	{
		rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.rekey (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, " ");
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
}

TEST (wallet, representative)
{
	auto error (false);
	rai::mdb_env env (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (error, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (rai::genesis_account, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	rai::keypair key;
	wallet.representative_set (transaction, key.pub);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (key.pub, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	wallet.insert_adhoc (transaction, key.prv);
	ASSERT_TRUE (wallet.is_representative (transaction));
}

TEST (wallet, serialize_json_empty)
{
	auto error (false);
	rai::mdb_env env (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet1 (error, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	rai::wallet_store wallet2 (error, kdf, transaction, rai::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	rai::raw_key password1;
	rai::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_EQ (wallet1.end (), wallet1.begin (transaction));
	ASSERT_EQ (wallet2.end (), wallet2.begin (transaction));
}

TEST (wallet, serialize_json_one)
{
	auto error (false);
	rai::mdb_env env (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet1 (error, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	rai::keypair key;
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	rai::wallet_store wallet2 (error, kdf, transaction, rai::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	rai::raw_key password1;
	rai::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	rai::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
	auto error (false);
	rai::mdb_env env (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet1 (error, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	rai::keypair key;
	wallet1.rekey (transaction, "password");
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	rai::wallet_store wallet2 (error, kdf, transaction, rai::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet2.valid_password (transaction));
	ASSERT_FALSE (wallet2.attempt_password (transaction, "password"));
	ASSERT_TRUE (wallet2.valid_password (transaction));
	rai::raw_key password1;
	rai::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	rai::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
	auto error (false);
	rai::mdb_env env (error, rai::unique_path ());
	ASSERT_FALSE (error);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet1 (error, kdf, transaction, rai::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	rai::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	rai::wallet_store wallet2 (error, kdf, transaction, rai::genesis_account, 1, "1");
	ASSERT_FALSE (error);
	rai::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
	std::vector<rai::public_key> keys;
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
	wallet1->insert_adhoc (key1.prv);
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
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, fail_import_corrupt)
{
	rai::system system (24000, 2);
	auto wallet1 (system.wallet (1));
	std::string json;
	auto error (wallet1->import (json, "1"));
	ASSERT_TRUE (error);
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (rai::test_genesis_key.prv);
	rai::genesis genesis;
	auto done (false);
	system.deadline_set (10s);
	while (!done)
	{
		rai::transaction transaction (system.nodes[0]->store.tx_begin ());
		uint64_t work (0);
		if (!wallet->store.work_get (transaction, rai::test_genesis_key.pub, work))
		{
			done = !rai::work_validate (genesis.hash (), work);
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_generate)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	rai::uint128_t amount1 (system.nodes[0]->balance (rai::test_genesis_key.pub));
	uint64_t work1;
	wallet->insert_adhoc (rai::test_genesis_key.prv);
	rai::account account1;
	{
		rai::transaction transaction (system.nodes[0]->store.tx_begin ());
		account1 = system.account (transaction, 0);
	}
	rai::keypair key;
	wallet->send_action (rai::test_genesis_key.pub, key.pub, 100);
	system.deadline_set (10s);
	auto transaction (system.nodes[0]->store.tx_begin ());
	while (system.nodes[0]->ledger.account_balance (transaction, rai::test_genesis_key.pub) == amount1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	auto again (true);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (system.nodes[0]->store.tx_begin ());
		again = wallet->store.work_get (transaction, account1, work1) || rai::work_validate (system.nodes[0]->ledger.latest_root (transaction, account1), work1);
	}
}

TEST (wallet, insert_locked)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	{
		auto transaction (wallet->wallets.tx_begin (true));
		wallet->store.rekey (transaction, "1");
		ASSERT_TRUE (wallet->store.valid_password (transaction));
		wallet->enter_password (transaction, "");
	}
	auto transaction (wallet->wallets.tx_begin ());
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->insert_adhoc (rai::keypair ().prv).is_zero ());
}

TEST (wallet, version_1_upgrade)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	rai::keypair key;
	auto transaction (wallet->wallets.tx_begin (true));
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	rai::raw_key password_l;
	rai::wallet_value value (wallet->store.entry_get_raw (transaction, rai::wallet_store::wallet_key_special));
	rai::raw_key kdf;
	kdf.data.clear ();
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	rai::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, rai::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (wallet->store.version_current, wallet->store.version (transaction));
	rai::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	value = wallet->store.entry_get_raw (transaction, rai::wallet_store::wallet_key_special);
	wallet->store.derive_key (kdf, transaction, "");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, rai::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (wallet->store.version_current, wallet->store.version (transaction));
	rai::raw_key prv2;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv2));
	ASSERT_EQ (key.prv, prv2);
}

TEST (wallet, deterministic_keys)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	rai::raw_key key1;
	wallet.deterministic_key (key1, transaction, 0);
	rai::raw_key key2;
	wallet.deterministic_key (key2, transaction, 0);
	ASSERT_EQ (key1, key2);
	rai::raw_key key3;
	wallet.deterministic_key (key3, transaction, 1);
	ASSERT_NE (key1, key3);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	auto key4 (wallet.deterministic_insert (transaction));
	rai::raw_key key5;
	ASSERT_FALSE (wallet.fetch (transaction, key4, key5));
	ASSERT_EQ (key3, key5);
	ASSERT_EQ (2, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.erase (transaction, key4);
	ASSERT_FALSE (wallet.exists (transaction, key4));
	auto key8 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key4, key8);
	auto key6 (wallet.deterministic_insert (transaction));
	rai::raw_key key7;
	ASSERT_FALSE (wallet.fetch (transaction, key6, key7));
	ASSERT_NE (key5, key7);
	ASSERT_EQ (3, wallet.deterministic_index_get (transaction));
	rai::keypair key9;
	ASSERT_EQ (key9.pub, wallet.insert_adhoc (transaction, key9.prv));
	ASSERT_TRUE (wallet.exists (transaction, key9.pub));
	wallet.deterministic_clear (transaction);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	ASSERT_FALSE (wallet.exists (transaction, key4));
	ASSERT_FALSE (wallet.exists (transaction, key6));
	ASSERT_FALSE (wallet.exists (transaction, key8));
	ASSERT_TRUE (wallet.exists (transaction, key9.pub));
}

TEST (wallet, reseed)
{
	bool init;
	rai::mdb_env env (init, rai::unique_path ());
	ASSERT_FALSE (init);
	rai::transaction transaction (env.tx_begin (true));
	rai::kdf kdf;
	rai::wallet_store wallet (init, kdf, transaction, rai::genesis_account, 1, "0");
	rai::raw_key seed1;
	seed1.data = 1;
	rai::raw_key seed2;
	seed2.data = 2;
	wallet.seed_set (transaction, seed1);
	rai::raw_key seed3;
	wallet.seed (seed3, transaction);
	ASSERT_EQ (seed1, seed3);
	auto key1 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.seed_set (transaction, seed2);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	rai::raw_key seed4;
	wallet.seed (seed4, transaction);
	ASSERT_EQ (seed2, seed4);
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_NE (key1, key2);
	wallet.seed_set (transaction, seed1);
	rai::raw_key seed5;
	wallet.seed (seed5, transaction);
	ASSERT_EQ (seed1, seed5);
	auto key3 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key1, key3);
}

TEST (wallet, insert_deterministic_locked)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin (true));
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_2_upgrade)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin (true));
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.attempt_password (transaction, ""));
	wallet->store.erase (transaction, rai::wallet_store::deterministic_index_special);
	wallet->store.erase (transaction, rai::wallet_store::seed_special);
	wallet->store.version_put (transaction, 2);
	ASSERT_EQ (2, wallet->store.version (transaction));
	ASSERT_FALSE (wallet->store.exists (transaction, rai::wallet_store::deterministic_index_special));
	ASSERT_FALSE (wallet->store.exists (transaction, rai::wallet_store::seed_special));
	wallet->store.attempt_password (transaction, "1");
	ASSERT_EQ (wallet->store.version_current, wallet->store.version (transaction));
	ASSERT_TRUE (wallet->store.exists (transaction, rai::wallet_store::deterministic_index_special));
	ASSERT_TRUE (wallet->store.exists (transaction, rai::wallet_store::seed_special));
	ASSERT_FALSE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_3_upgrade)
{
	rai::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin (true));
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (wallet->store.version_current, wallet->store.version (transaction));
	rai::keypair key;
	rai::raw_key seed;
	rai::uint256_union seed_ciphertext;
	rai::random_pool.GenerateBlock (seed.data.bytes.data (), seed.data.bytes.size ());
	rai::raw_key password_l;
	rai::wallet_value value (wallet->store.entry_get_raw (transaction, rai::wallet_store::wallet_key_special));
	rai::raw_key kdf;
	wallet->store.derive_key (kdf, transaction, "1");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	rai::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, rai::wallet_value (ciphertext, 0));
	seed_ciphertext.encrypt (seed, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, rai::wallet_store::seed_special, rai::wallet_value (seed_ciphertext, 0));
	wallet->store.version_put (transaction, 3);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (wallet->store.version_current, wallet->store.version (transaction));
	rai::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	rai::raw_key seed_compare;
	wallet->store.seed (seed_compare, transaction);
	ASSERT_EQ (seed, seed_compare);
	ASSERT_NE (seed_ciphertext, wallet->store.entry_get_raw (transaction, rai::wallet_store::seed_special).key);
}

TEST (wallet, no_work)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv, false);
	rai::keypair key2;
	auto block (system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, std::numeric_limits<rai::uint128_t>::max (), false));
	ASSERT_NE (nullptr, block);
	ASSERT_NE (0, block->block_work ());
	ASSERT_FALSE (rai::work_validate (block->root (), block->block_work ()));
	auto transaction (system.nodes[0]->store.tx_begin ());
	uint64_t cached_work (0);
	system.wallet (0)->store.work_get (transaction, rai::test_genesis_key.pub, cached_work);
	ASSERT_EQ (0, cached_work);
}

TEST (wallet, send_race)
{
	rai::system system (24000, 1);
	system.wallet (0)->insert_adhoc (rai::test_genesis_key.prv);
	rai::keypair key2;
	for (auto i (1); i < 60; ++i)
	{
		ASSERT_NE (nullptr, system.wallet (0)->send_action (rai::test_genesis_key.pub, key2.pub, rai::Gxrb_ratio));
		ASSERT_EQ (rai::genesis_amount - rai::Gxrb_ratio * i, system.nodes[0]->balance (rai::test_genesis_key.pub));
	}
}

TEST (wallet, password_race)
{
	rai::system system (24000, 1);
	rai::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	system.nodes[0]->background ([&wallet]() {
		for (int i = 0; i < 100; i++)
		{
			auto transaction (wallet->wallets.tx_begin (true));
			wallet->store.rekey (transaction, std::to_string (i));
		}
	});
	for (int i = 0; i < 100; i++)
	{
		auto transaction (wallet->wallets.tx_begin ());
		// Password should always be valid, the rekey operation should be atomic.
		bool ok = wallet->store.valid_password (transaction);
		EXPECT_TRUE (ok);
		if (!ok)
		{
			break;
		}
	}
	system.stop ();
	runner.join ();
}

TEST (wallet, password_race_corrupt_seed)
{
	rai::system system (24000, 1);
	rai::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	rai::raw_key seed;
	{
		auto transaction (wallet->wallets.tx_begin (true));
		ASSERT_FALSE (wallet->store.rekey (transaction, "4567"));
		wallet->store.seed (seed, transaction);
		ASSERT_FALSE (wallet->store.attempt_password (transaction, "4567"));
	}
	for (int i = 0; i < 100; i++)
	{
		system.nodes[0]->background ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin (true));
				wallet->store.rekey (transaction, "0000");
			}
		});
		system.nodes[0]->background ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin (true));
				wallet->store.rekey (transaction, "1234");
			}
		});
		system.nodes[0]->background ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin ());
				wallet->store.attempt_password (transaction, "1234");
			}
		});
	}
	system.stop ();
	runner.join ();
	{
		auto transaction (wallet->wallets.tx_begin (true));
		if (!wallet->store.attempt_password (transaction, "1234"))
		{
			rai::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "0000"))
		{
			rai::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "4567"))
		{
			rai::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else
		{
			ASSERT_FALSE (true);
		}
	}
}
