#include <nano/core_test/testutil.hpp>
#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/lmdb/wallet_value.hpp>
#include <nano/node/testing.hpp>

#include <gtest/gtest.h>

#include <fstream>

using namespace std::chrono_literals;
unsigned constexpr nano::wallet_store::version_current;

TEST (wallet, no_special_keys_accounts)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));

	for (uint64_t account = 0; account < nano::wallet_store::special_count; account++)
	{
		nano::account account_l (account);
		ASSERT_FALSE (wallet.exists (transaction, account_l));
	}
}

TEST (wallet, no_key)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::keypair key1;
	nano::raw_key prv1;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet, fetch_locked)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_TRUE (wallet.valid_password (transaction));
	nano::keypair key1;
	ASSERT_EQ (key1.pub, wallet.insert_adhoc (transaction, key1.prv));
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_FALSE (key2.is_zero ());
	nano::raw_key key3;
	key3.data = 1;
	wallet.password.value_set (key3);
	ASSERT_FALSE (wallet.valid_password (transaction));
	nano::raw_key key4;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, key4));
	ASSERT_TRUE (wallet.fetch (transaction, key2, key4));
}

TEST (wallet, retrieval)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::keypair key1;
	ASSERT_TRUE (wallet.valid_password (transaction));
	wallet.insert_adhoc (transaction, key1.prv);
	nano::raw_key prv1;
	ASSERT_FALSE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
	ASSERT_EQ (key1.prv, prv1);
	wallet.password.values[0]->bytes[16] ^= 1;
	nano::raw_key prv2;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv2));
	ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet, empty_iteration)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	auto i (wallet.begin (transaction));
	auto j (wallet.end ());
	ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
	{
		ASSERT_EQ (key1.pub, nano::uint256_union (i->first));
		nano::raw_key password;
		wallet.wallet_key (password, transaction);
		nano::raw_key key;
		key.decrypt (nano::wallet_value (i->second).key, password, (nano::uint256_union (i->first)).owords[0].number ());
		ASSERT_EQ (key1.prv, key);
	}
}

TEST (wallet, two_item_iteration)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set<nano::public_key> pubs;
	std::unordered_set<nano::private_key> prvs;
	nano::kdf kdf;
	{
		auto transaction (env.tx_begin_write ());
		nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.insert_adhoc (transaction, key1.prv);
		wallet.insert_adhoc (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end ()); i != j; ++i)
		{
			pubs.insert (i->first);
			nano::raw_key password;
			wallet.wallet_key (password, transaction);
			nano::raw_key key;
			key.decrypt (nano::wallet_value (i->second).key, password, (i->first).owords[0].number ());
			prvs.insert (key.as_private_key ());
		}
	}
	ASSERT_EQ (2, pubs.size ());
	ASSERT_EQ (2, prvs.size ());
	ASSERT_NE (pubs.end (), pubs.find (key1.pub));
	ASSERT_NE (prvs.end (), prvs.find (key1.prv.as_private_key ()));
	ASSERT_NE (pubs.end (), pubs.find (key2.pub));
	ASSERT_NE (prvs.end (), prvs.find (key2.prv.as_private_key ()));
}

TEST (wallet, insufficient_spend_one)
{
	nano::system system (24000, 1);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (nano::test_genesis_key.pub, key1.pub, 500));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key1.pub, nano::genesis_amount));
}

TEST (wallet, spend_all_one)
{
	nano::system system (24000, 1);
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max ()));
	nano::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		system.nodes[0]->store.account_get (transaction, nano::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes[0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, system.nodes[0]->balance (nano::test_genesis_key.pub));
}

TEST (wallet, send_async)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	boost::thread thread ([&system]() {
		system.deadline_set (10s);
		while (!system.nodes[0]->balance (nano::test_genesis_key.pub).is_zero ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	});
	std::atomic<bool> success (false);
	system.wallet (0)->send_async (nano::test_genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max (), [&success](std::shared_ptr<nano::block> block_a) { ASSERT_NE (nullptr, block_a); success = true; });
	thread.join ();
	ASSERT_TRUE (success);
}

TEST (wallet, spend)
{
	nano::system system (24000, 1);
	nano::block_hash latest1 (system.nodes[0]->latest (nano::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (0, key2.pub, 0));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max ()));
	nano::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		system.nodes[0]->store.account_get (transaction, nano::test_genesis_key.pub, info2);
		ASSERT_NE (latest1, info2.head);
		auto block (system.nodes[0]->store.block_get (transaction, info2.head));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (latest1, block->previous ());
	}
	ASSERT_TRUE (info2.balance.is_zero ());
	ASSERT_EQ (0, system.nodes[0]->balance (nano::test_genesis_key.pub));
}

TEST (wallet, change)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	auto block1 (system.nodes[0]->rep_block (nano::test_genesis_key.pub));
	ASSERT_FALSE (block1.is_zero ());
	ASSERT_NE (nullptr, system.wallet (0)->change_action (nano::test_genesis_key.pub, key2.pub));
	auto block2 (system.nodes[0]->rep_block (nano::test_genesis_key.pub));
	ASSERT_FALSE (block2.is_zero ());
	ASSERT_NE (block1, block2);
}

TEST (wallet, partial_spend)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - 500, system.nodes[0]->balance (nano::test_genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
	nano::system system (24000, 1);
	{
		system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		nano::account_info info1;
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, nano::test_genesis_key.pub, info1));
		for (auto i (0); i < 50; ++i)
		{
			nano::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);
		}
	}
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - 500, system.nodes[0]->balance (nano::test_genesis_key.pub));
}

TEST (wallet, find_none)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::account account (1000);
	ASSERT_EQ (wallet.end (), wallet.find (transaction, account));
}

TEST (wallet, find_existing)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::keypair key1;
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
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::raw_key password;
	wallet.password.value (password);
	ASSERT_TRUE (password.data.is_zero ());
	ASSERT_FALSE (init);
	nano::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	nano::raw_key prv1;
	wallet.fetch (transaction, key1.pub, prv1);
	ASSERT_EQ (key1.prv, prv1);
	ASSERT_FALSE (wallet.rekey (transaction, "1"));
	wallet.password.value (password);
	nano::raw_key password1;
	wallet.derive_key (password1, transaction, "1");
	ASSERT_EQ (password1, password);
	nano::raw_key prv2;
	wallet.fetch (transaction, key1.pub, prv2);
	ASSERT_EQ (key1.prv, prv2);
	*wallet.password.values[0] = 2;
	ASSERT_TRUE (wallet.rekey (transaction, "2"));
}

TEST (account, encode_zero)
{
	nano::account number0 (0);
	std::string str0;
	number0.encode_account (str0);

	/*
	 * Handle different lengths for "xrb_" prefixed and "nano_" prefixed accounts
	 */
	ASSERT_EQ ((str0.front () == 'x') ? 64 : 65, str0.size ());
	ASSERT_EQ (65, str0.size ());
	nano::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_all)
{
	nano::account number0;
	number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	std::string str0;
	number0.encode_account (str0);

	/*
	 * Handle different lengths for "xrb_" prefixed and "nano_" prefixed accounts
	 */
	ASSERT_EQ ((str0.front () == 'x') ? 64 : 65, str0.size ());
	nano::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_fail)
{
	nano::account number0 (0);
	std::string str0;
	number0.encode_account (str0);
	str0[16] ^= 1;
	nano::account number1;
	ASSERT_TRUE (number1.decode_account (str0));
}

TEST (wallet, hash_password)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (init);
	nano::raw_key hash1;
	wallet.derive_key (hash1, transaction, "");
	nano::raw_key hash2;
	wallet.derive_key (hash2, transaction, "");
	ASSERT_EQ (hash1, hash2);
	nano::raw_key hash3;
	wallet.derive_key (hash3, transaction, "a");
	ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
	nano::uint256_union value0 (0);
	nano::fan fan (value0, 1024);
	for (auto & i : fan.values)
	{
		ASSERT_NE (value0, *i);
	}
	nano::raw_key value1;
	fan.value (value1);
	ASSERT_EQ (value0, value1.data);
}

TEST (fan, change)
{
	nano::raw_key value0;
	value0.data = 0;
	nano::raw_key value1;
	value1.data = 1;
	ASSERT_NE (value0, value1);
	nano::fan fan (value0.data, 1024);
	ASSERT_EQ (1024, fan.values.size ());
	nano::raw_key value2;
	fan.value (value2);
	ASSERT_EQ (value0, value2);
	fan.value_set (value1);
	fan.value (value2);
	ASSERT_EQ (value1, value2);
}

TEST (wallet, reopen_default_password)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	auto transaction (env.tx_begin_write ());
	ASSERT_FALSE (init);
	nano::kdf kdf;
	{
		nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
		ASSERT_FALSE (init);
		wallet.rekey (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
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
	nano::mdb_env env (error, nano::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (error, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (nano::genesis_account, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	nano::keypair key;
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
	nano::mdb_env env (error, nano::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet1 (error, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet_store wallet2 (error, kdf, transaction, nano::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	nano::raw_key password1;
	nano::raw_key password2;
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
	nano::mdb_env env (error, nano::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet1 (error, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	nano::keypair key;
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet_store wallet2 (error, kdf, transaction, nano::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	nano::raw_key password1;
	nano::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	nano::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
	auto error (false);
	nano::mdb_env env (error, nano::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet1 (error, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	nano::keypair key;
	wallet1.rekey (transaction, "password");
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet_store wallet2 (error, kdf, transaction, nano::genesis_account, 1, "1", serialized);
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet2.valid_password (transaction));
	ASSERT_FALSE (wallet2.attempt_password (transaction, "password"));
	ASSERT_TRUE (wallet2.valid_password (transaction));
	nano::raw_key password1;
	nano::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	nano::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
	auto error (false);
	nano::mdb_env env (error, nano::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet1 (error, kdf, transaction, nano::genesis_account, 1, "0");
	ASSERT_FALSE (error);
	nano::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	nano::wallet_store wallet2 (error, kdf, transaction, nano::genesis_account, 1, "1");
	ASSERT_FALSE (error);
	nano::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
	std::vector<nano::public_key> keys;
	keys.push_back (key2.pub);
	ASSERT_FALSE (wallet1.move (transaction, wallet2, keys));
	ASSERT_TRUE (wallet1.exists (transaction, key2.pub));
	ASSERT_FALSE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, import)
{
	nano::system system (24000, 2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	nano::keypair key1;
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
	nano::system system (24000, 2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	nano::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, fail_import_corrupt)
{
	nano::system system (24000, 2);
	auto wallet1 (system.wallet (1));
	std::string json;
	auto error (wallet1->import (json, "1"));
	ASSERT_TRUE (error);
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (nano::test_genesis_key.prv);
	nano::genesis genesis;
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		uint64_t work (0);
		if (!wallet->store.work_get (transaction, nano::test_genesis_key.pub, work))
		{
			done = !nano::work_validate (genesis.hash (), work);
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_generate)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	nano::uint128_t amount1 (system.nodes[0]->balance (nano::test_genesis_key.pub));
	uint64_t work1;
	wallet->insert_adhoc (nano::test_genesis_key.prv);
	nano::account account1;
	{
		auto transaction (system.nodes[0]->wallets.tx_begin_read ());
		account1 = system.account (transaction, 0);
	}
	nano::keypair key;
	wallet->send_action (nano::test_genesis_key.pub, key.pub, 100);
	system.deadline_set (10s);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	while (system.nodes[0]->ledger.account_balance (transaction, nano::test_genesis_key.pub) == amount1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	auto again (true);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto block_transaction (system.nodes[0]->store.tx_begin_read ());
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		again = wallet->store.work_get (transaction, account1, work1) || nano::work_validate (system.nodes[0]->ledger.latest_root (block_transaction, account1), work1);
	}
}

TEST (wallet, insert_locked)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->store.rekey (transaction, "1");
		ASSERT_TRUE (wallet->store.valid_password (transaction));
		wallet->enter_password (transaction, "");
	}
	auto transaction (wallet->wallets.tx_begin_read ());
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->insert_adhoc (nano::keypair ().prv).is_zero ());
}

TEST (wallet, version_1_upgrade)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	nano::keypair key;
	auto transaction (wallet->wallets.tx_begin_write ());
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	nano::raw_key password_l;
	nano::wallet_value value (wallet->store.entry_get_raw (transaction, nano::wallet_store::wallet_key_special));
	nano::raw_key kdf;
	kdf.data.clear ();
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	nano::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, nano::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (nano::wallet_store::version_current, wallet->store.version (transaction));
	nano::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	value = wallet->store.entry_get_raw (transaction, nano::wallet_store::wallet_key_special);
	wallet->store.derive_key (kdf, transaction, "");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, nano::wallet_value (ciphertext, 0));
	wallet->store.version_put (transaction, 1);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (nano::wallet_store::version_current, wallet->store.version (transaction));
	nano::raw_key prv2;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv2));
	ASSERT_EQ (key.prv, prv2);
}

TEST (wallet, deterministic_keys)
{
	bool init;
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	auto key1 = wallet.deterministic_key (transaction, 0);
	auto key2 = wallet.deterministic_key (transaction, 0);
	ASSERT_EQ (key1, key2);
	auto key3 = wallet.deterministic_key (transaction, 1);
	ASSERT_NE (key1, key3);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	auto key4 (wallet.deterministic_insert (transaction));
	nano::raw_key key5;
	ASSERT_FALSE (wallet.fetch (transaction, key4, key5));
	ASSERT_EQ (key3, key5.as_private_key ());
	ASSERT_EQ (2, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.erase (transaction, key4);
	ASSERT_FALSE (wallet.exists (transaction, key4));
	auto key8 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key4, key8);
	auto key6 (wallet.deterministic_insert (transaction));
	nano::raw_key key7;
	ASSERT_FALSE (wallet.fetch (transaction, key6, key7));
	ASSERT_NE (key5, key7);
	ASSERT_EQ (3, wallet.deterministic_index_get (transaction));
	nano::keypair key9;
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
	nano::mdb_env env (init, nano::unique_path ());
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	nano::kdf kdf;
	nano::wallet_store wallet (init, kdf, transaction, nano::genesis_account, 1, "0");
	nano::raw_key seed1;
	seed1.data = 1;
	nano::raw_key seed2;
	seed2.data = 2;
	wallet.seed_set (transaction, seed1);
	nano::raw_key seed3;
	wallet.seed (seed3, transaction);
	ASSERT_EQ (seed1, seed3);
	auto key1 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.seed_set (transaction, seed2);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	nano::raw_key seed4;
	wallet.seed (seed4, transaction);
	ASSERT_EQ (seed2, seed4);
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_NE (key1, key2);
	wallet.seed_set (transaction, seed1);
	nano::raw_key seed5;
	wallet.seed (seed5, transaction);
	ASSERT_EQ (seed1, seed5);
	auto key3 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key1, key3);
}

TEST (wallet, insert_deterministic_locked)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_2_upgrade)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.attempt_password (transaction, ""));
	wallet->store.erase (transaction, nano::wallet_store::deterministic_index_special);
	wallet->store.erase (transaction, nano::wallet_store::seed_special);
	wallet->store.version_put (transaction, 2);
	ASSERT_EQ (2, wallet->store.version (transaction));
	ASSERT_EQ (wallet->store.find (transaction, nano::wallet_store::deterministic_index_special), wallet->store.end ());
	ASSERT_EQ (wallet->store.find (transaction, nano::wallet_store::seed_special), wallet->store.end ());
	wallet->store.attempt_password (transaction, "1");
	ASSERT_EQ (nano::wallet_store::version_current, wallet->store.version (transaction));
	ASSERT_NE (wallet->store.find (transaction, nano::wallet_store::deterministic_index_special), wallet->store.end ());
	ASSERT_NE (wallet->store.find (transaction, nano::wallet_store::seed_special), wallet->store.end ());
	ASSERT_FALSE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, version_3_upgrade)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (nano::wallet_store::version_current, wallet->store.version (transaction));
	nano::keypair key;
	nano::raw_key seed;
	nano::uint256_union seed_ciphertext;
	nano::random_pool::generate_block (seed.data.bytes.data (), seed.data.bytes.size ());
	nano::raw_key password_l;
	nano::wallet_value value (wallet->store.entry_get_raw (transaction, nano::wallet_store::wallet_key_special));
	nano::raw_key kdf;
	wallet->store.derive_key (kdf, transaction, "1");
	password_l.decrypt (value.key, kdf, wallet->store.salt (transaction).owords[0]);
	nano::uint256_union ciphertext;
	ciphertext.encrypt (key.prv, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, key.pub, nano::wallet_value (ciphertext, 0));
	seed_ciphertext.encrypt (seed, password_l, wallet->store.salt (transaction).owords[0]);
	wallet->store.entry_put_raw (transaction, nano::wallet_store::seed_special, nano::wallet_value (seed_ciphertext, 0));
	wallet->store.version_put (transaction, 3);
	wallet->enter_password (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	ASSERT_EQ (nano::wallet_store::version_current, wallet->store.version (transaction));
	nano::raw_key prv;
	ASSERT_FALSE (wallet->store.fetch (transaction, key.pub, prv));
	ASSERT_EQ (key.prv, prv);
	nano::raw_key seed_compare;
	wallet->store.seed (seed_compare, transaction);
	ASSERT_EQ (seed, seed_compare);
	ASSERT_NE (seed_ciphertext, wallet->store.entry_get_raw (transaction, nano::wallet_store::seed_special).key);
}

TEST (wallet, upgrade_backup)
{
	nano::system system (24000, 1);
	auto dir (nano::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	/** Returns 'dir' if backup file cannot be found */
	// clang-format off
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("wallets_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};
	// clang-format on

	auto wallet_id = nano::random_wallet_id ();
	{
		auto node1 (std::make_shared<nano::node> (system.io_ctx, 24001, dir, system.alarm, system.logging, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.create (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_write ());
		wallet->store.version_put (transaction, 3);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Check with config backup_before_upgrade = false
	{
		auto node1 (std::make_shared<nano::node> (system.io_ctx, 24001, dir, system.alarm, system.logging, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.open (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_write ());
		ASSERT_LT (3u, wallet->store.version (transaction));
		wallet->store.version_put (transaction, 3);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	{
		nano::node_config node_config (24001, system.logging);
		node_config.backup_before_upgrade = true;
		auto node1 (std::make_shared<nano::node> (system.io_ctx, dir, system.alarm, node_config, system.work));
		ASSERT_FALSE (node1->init_error ());
		auto wallet (node1->wallets.open (wallet_id));
		ASSERT_NE (nullptr, wallet);
		auto transaction (node1->wallets.tx_begin_read ());
		ASSERT_LT (3u, wallet->store.version (transaction));
	}
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

TEST (wallet, no_work)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv, false);
	nano::keypair key2;
	auto block (system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max (), false));
	ASSERT_NE (nullptr, block);
	ASSERT_NE (0, block->block_work ());
	ASSERT_FALSE (nano::work_validate (block->root (), block->block_work ()));
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	uint64_t cached_work (0);
	system.wallet (0)->store.work_get (transaction, nano::test_genesis_key.pub, cached_work);
	ASSERT_EQ (0, cached_work);
}

TEST (wallet, send_race)
{
	nano::system system (24000, 1);
	system.wallet (0)->insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key2;
	for (auto i (1); i < 60; ++i)
	{
		ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::test_genesis_key.pub, key2.pub, nano::Gxrb_ratio));
		ASSERT_EQ (nano::genesis_amount - nano::Gxrb_ratio * i, system.nodes[0]->balance (nano::test_genesis_key.pub));
	}
}

TEST (wallet, password_race)
{
	nano::system system (24000, 1);
	nano::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	std::thread thread ([&wallet]() {
		for (int i = 0; i < 100; i++)
		{
			auto transaction (wallet->wallets.tx_begin_write ());
			wallet->store.rekey (transaction, std::to_string (i));
		}
	});
	for (int i = 0; i < 100; i++)
	{
		auto transaction (wallet->wallets.tx_begin_read ());
		// Password should always be valid, the rekey operation should be atomic.
		bool ok = wallet->store.valid_password (transaction);
		EXPECT_TRUE (ok);
		if (!ok)
		{
			break;
		}
	}
	thread.join ();
	system.stop ();
	runner.join ();
}

TEST (wallet, password_race_corrupt_seed)
{
	nano::system system (24000, 1);
	nano::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	nano::raw_key seed;
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		ASSERT_FALSE (wallet->store.rekey (transaction, "4567"));
		wallet->store.seed (seed, transaction);
		ASSERT_FALSE (wallet->store.attempt_password (transaction, "4567"));
	}
	std::vector<std::thread> threads;
	for (int i = 0; i < 100; i++)
	{
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "0000");
			}
		});
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "1234");
			}
		});
		threads.emplace_back ([&wallet]() {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_read ());
				wallet->store.attempt_password (transaction, "1234");
			}
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
	system.stop ();
	runner.join ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		if (!wallet->store.attempt_password (transaction, "1234"))
		{
			nano::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "0000"))
		{
			nano::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else if (!wallet->store.attempt_password (transaction, "4567"))
		{
			nano::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_TRUE (seed_now == seed);
		}
		else
		{
			ASSERT_FALSE (true);
		}
	}
}

TEST (wallet, change_seed)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	nano::raw_key seed1;
	seed1.data = 1;
	nano::public_key pub;
	uint32_t index (4);
	auto prv = nano::deterministic_key (seed1, index);
	pub = nano::pub_key (prv);
	wallet->insert_adhoc (nano::test_genesis_key.prv, false);
	auto block (wallet->send_action (nano::test_genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	system.nodes[0]->block_processor.flush ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		nano::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, deterministic_restore)
{
	nano::system system (24000, 1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	nano::raw_key seed1;
	seed1.data = 1;
	nano::public_key pub;
	uint32_t index (4);
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		nano::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (1, wallet->store.deterministic_index_get (transaction));
		auto prv = nano::deterministic_key (seed1, index);
		pub = nano::pub_key (prv);
	}
	wallet->insert_adhoc (nano::test_genesis_key.prv, false);
	auto block (wallet->send_action (nano::test_genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	system.nodes[0]->block_processor.flush ();
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->deterministic_restore (transaction);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, work_watcher_update)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key;
	auto const block1 (wallet.send_action (nano::test_genesis_key.pub, key.pub, 100));
	uint64_t difficulty1 (0);
	nano::work_validate (*block1, &difficulty1);
	auto const block2 (wallet.send_action (nano::test_genesis_key.pub, key.pub, 200));
	uint64_t difficulty2 (0);
	nano::work_validate (*block2, &difficulty2);
	auto multiplier = nano::difficulty::to_multiplier (std::max (difficulty1, difficulty2), node.network_params.network.publish_threshold);
	uint64_t updated_difficulty1{ difficulty1 }, updated_difficulty2{ difficulty2 };
	{
		nano::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		//fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (multiplier * (1.5 + i / 100.));
		}
		node.active.update_active_difficulty (lock);
	}
	system.deadline_set (20s);
	while (updated_difficulty1 == difficulty1 || updated_difficulty2 == difficulty2)
	{
		{
			nano::lock_guard<std::mutex> guard (node.active.mutex);
			{
				auto const existing (node.active.roots.find (block1->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_difficulty1 = existing->difficulty;
			}
			{
				auto const existing (node.active.roots.find (block2->qualified_root ()));
				//if existing is junk the block has been confirmed already
				ASSERT_NE (existing, node.active.roots.end ());
				updated_difficulty2 = existing->difficulty;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_GT (updated_difficulty1, difficulty1);
	ASSERT_GT (updated_difficulty2, difficulty2);
}

TEST (wallet, work_watcher_generation_disabled)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	node_config.work_watcher_period = 1s;
	node_config.work_threads = 0;
	auto & node = *system.add_node (node_config);
	nano::work_pool pool (std::numeric_limits<unsigned>::max ());
	nano::genesis genesis;
	nano::keypair key;
	auto block (std::make_shared<nano::state_block> (nano::test_genesis_key.pub, genesis.hash (), nano::test_genesis_key.pub, nano::genesis_amount - nano::Mxrb_ratio, key.pub, nano::test_genesis_key.prv, nano::test_genesis_key.pub, *pool.generate (genesis.hash ())));
	uint64_t difficulty (0);
	ASSERT_FALSE (nano::work_validate (*block, &difficulty));
	node.wallets.watcher->add (block);
	ASSERT_FALSE (node.process_local (block).code != nano::process_result::progress);
	ASSERT_TRUE (node.wallets.watcher->is_watched (block->qualified_root ()));
	auto multiplier = nano::difficulty::to_multiplier (difficulty, node.network_params.network.publish_threshold);
	uint64_t updated_difficulty{ difficulty };
	{
		nano::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		//fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (multiplier * (1.5 + i / 100.));
		}
		node.active.update_active_difficulty (lock);
	}
	std::this_thread::sleep_for (5s);

	nano::lock_guard<std::mutex> guard (node.active.mutex);
	{
		auto const existing (node.active.roots.find (block->qualified_root ()));
		//if existing is junk the block has been confirmed already
		ASSERT_NE (existing, node.active.roots.end ());
		updated_difficulty = existing->difficulty;
	}
	ASSERT_EQ (updated_difficulty, difficulty);
	ASSERT_TRUE (node.distributed_work.work.empty ());
}

TEST (wallet, work_watcher_removed)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.work_watcher_period = 1s;
	auto & node = *system.add_node (node_config);
	(void)node;
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::test_genesis_key.prv);
	nano::keypair key;
	ASSERT_EQ (0, wallet.wallets.watcher->size ());
	auto const block (wallet.send_action (nano::test_genesis_key.pub, key.pub, 100));
	ASSERT_EQ (1, wallet.wallets.watcher->size ());
	auto transaction (wallet.wallets.tx_begin_write ());
	system.deadline_set (3s);
	while (0 == wallet.wallets.watcher->size ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_watcher_cancel)
{
	nano::system system;
	nano::node_config node_config (24000, system.logging);
	node_config.work_watcher_period = 1s;
	node_config.max_work_generate_multiplier = 1e6;
	node_config.max_work_generate_difficulty = nano::difficulty::from_multiplier (node_config.max_work_generate_multiplier, nano::network_constants::publish_test_threshold);
	node_config.enable_voting = false;
	auto & node = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	wallet.insert_adhoc (nano::test_genesis_key.prv, false);
	nano::keypair key;
	auto work1 (node.work_generate_blocking (nano::test_genesis_key.pub));
	auto const block1 (wallet.send_action (nano::test_genesis_key.pub, key.pub, 100, *work1, false));
	uint64_t difficulty1 (0);
	nano::work_validate (*block1, &difficulty1);
	{
		nano::unique_lock<std::mutex> lock (node.active.mutex);
		// Prevent active difficulty repopulating multipliers
		node.network_params.network.request_interval_ms = 10000;
		// Fill multipliers_cb and update active difficulty;
		for (auto i (0); i < node.active.multipliers_cb.size (); i++)
		{
			node.active.multipliers_cb.push_back (node.config.max_work_generate_multiplier);
		}
		node.active.update_active_difficulty (lock);
	}
	// Wait for work generation to start
	system.deadline_set (5s);
	while (0 == node.work.size ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	// Cancel the ongoing work
	ASSERT_EQ (1, node.work.size ());
	node.work.cancel (block1->root ());
	ASSERT_EQ (0, node.work.size ());
	{
		nano::unique_lock<std::mutex> lock (wallet.wallets.watcher->mutex);
		auto existing (wallet.wallets.watcher->watched.find (block1->qualified_root ()));
		ASSERT_NE (wallet.wallets.watcher->watched.end (), existing);
		auto block2 (existing->second);
		// Block must be the same
		ASSERT_NE (nullptr, block1);
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (*block1, *block2);
		// but should still be under watch
		lock.unlock ();
		ASSERT_TRUE (wallet.wallets.watcher->is_watched (block1->qualified_root ()));
	}
}
