#include <celerix/crypto_lib/random_pool.hpp>
#include <celerix/lib/blocks.hpp>
#include <celerix/lib/thread_runner.hpp>
#include <celerix/lib/work_version.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/election.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/store/lmdb/wallet_value.hpp>
#include <celerix/test_common/system.hpp>
#include <celerix/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
unsigned constexpr celerix::wallet_store::version_current;

TEST (wallet, no_special_keys_accounts)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));

	for (uint64_t account = 0; account < celerix::wallet_store::special_count; account++)
	{
		celerix::account account_l (account);
		ASSERT_FALSE (wallet.exists (transaction, account_l));
	}
}

TEST (wallet, no_key)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	celerix::raw_key prv1;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet, fetch_locked)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_TRUE (wallet.valid_password (transaction));
	celerix::keypair key1;
	ASSERT_EQ (key1.pub, wallet.insert_adhoc (transaction, key1.prv));
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_FALSE (key2.is_zero ());
	celerix::raw_key key3;
	key3 = 1;
	wallet.password.value_set (key3);
	ASSERT_FALSE (wallet.valid_password (transaction));
	celerix::raw_key key4;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, key4));
	ASSERT_TRUE (wallet.fetch (transaction, key2, key4));
}

TEST (wallet, retrieval)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	ASSERT_TRUE (wallet.valid_password (transaction));
	wallet.insert_adhoc (transaction, key1.prv);
	celerix::raw_key prv1;
	ASSERT_FALSE (wallet.fetch (transaction, key1.pub, prv1));
	ASSERT_TRUE (wallet.valid_password (transaction));
	ASSERT_EQ (key1.prv, prv1);
	wallet.password.values[0]->bytes[16] ^= 1;
	celerix::raw_key prv2;
	ASSERT_TRUE (wallet.fetch (transaction, key1.pub, prv2));
	ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet, empty_iteration)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	auto i (wallet.begin (transaction));
	auto j (wallet.end (transaction));
	ASSERT_EQ (i, j);
}

TEST (wallet, one_item_iteration)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	for (auto i (wallet.begin (transaction)), j (wallet.end (transaction)); i != j; ++i)
	{
		ASSERT_EQ (key1.pub, celerix::uint256_union (i->first));
		celerix::raw_key password;
		wallet.wallet_key (password, transaction);
		celerix::raw_key key;
		key.decrypt (celerix::wallet_value (i->second).key, password, (celerix::uint256_union (i->first)).owords[0].number ());
		ASSERT_EQ (key1.prv, key);
	}
}

TEST (wallet, two_item_iteration)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	celerix::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set<celerix::public_key> pubs;
	std::unordered_set<celerix::raw_key> prvs;
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	{
		auto transaction (env.tx_begin_write ());
		celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
		ASSERT_FALSE (init);
		wallet.insert_adhoc (transaction, key1.prv);
		wallet.insert_adhoc (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end (transaction)); i != j; ++i)
		{
			pubs.insert (i->first);
			celerix::raw_key password;
			wallet.wallet_key (password, transaction);
			celerix::raw_key key;
			key.decrypt (celerix::wallet_value (i->second).key, password, (i->first).owords[0].number ());
			prvs.insert (key);
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
	celerix::test::system system (1);
	celerix::keypair key1;
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	auto block (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key1.pub, 500));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key1.pub, celerix::dev::constants.genesis_amount));
}

TEST (wallet, spend_all_one)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_hash latest1 (node1.latest (celerix::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, std::numeric_limits<celerix::uint128_t>::max ()));
	auto transaction = node1.ledger.tx_begin_read ();
	auto info2 = node1.ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_NE (latest1, info2->head);
	auto block = node1.ledger.any.block_get (transaction, info2->head);
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (latest1, block->previous ());
	ASSERT_TRUE (info2->balance.is_zero ());
	ASSERT_EQ (0, node1.balance (celerix::dev::genesis_key.pub));
}

TEST (wallet, send_async)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	std::thread thread ([&system] () {
		ASSERT_TIMELY (10s, system.nodes[0]->balance (celerix::dev::genesis_key.pub).is_zero ());
	});
	std::atomic<bool> success (false);
	system.wallet (0)->send_async (celerix::dev::genesis_key.pub, key2.pub, std::numeric_limits<celerix::uint128_t>::max (), [&success] (std::shared_ptr<celerix::block> const & block_a) { ASSERT_NE (nullptr, block_a); success = true; });
	thread.join ();
	ASSERT_TIMELY (2s, success);
}

TEST (wallet, spend)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	celerix::block_hash latest1 (node1.latest (celerix::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (0, key2.pub, 0));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, std::numeric_limits<celerix::uint128_t>::max ()));
	auto transaction = node1.ledger.tx_begin_read ();
	auto info2 = node1.ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_NE (latest1, info2->head);
	auto block = node1.ledger.any.block_get (transaction, info2->head);
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (latest1, block->previous ());
	ASSERT_TRUE (info2->balance.is_zero ());
	ASSERT_EQ (0, node1.balance (celerix::dev::genesis_key.pub));
}

TEST (wallet, partial_spend)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - 500, system.nodes[0]->balance (celerix::dev::genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
	celerix::test::system system (1);
	{
		system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
		auto transaction = system.nodes[0]->ledger.tx_begin_read ();
		auto info1 = system.nodes[0]->ledger.any.account_get (transaction, celerix::dev::genesis_key.pub);
		ASSERT_TRUE (info1);
		for (auto i (0); i < 50; ++i)
		{
			celerix::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);
		}
	}
	celerix::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<celerix::uint128_t>::max () - 500, system.nodes[0]->balance (celerix::dev::genesis_key.pub));
}

TEST (wallet, find_none)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::account account (1000);
	ASSERT_EQ (wallet.end (transaction), wallet.find (transaction, account));
}

TEST (wallet, find_existing)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));
	auto existing (wallet.find (transaction, key1.pub));
	ASSERT_NE (wallet.end (transaction), existing);
	++existing;
	ASSERT_EQ (wallet.end (transaction), existing);
}

TEST (wallet, rekey)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::raw_key password;
	wallet.password.value (password);
	ASSERT_TRUE (password.is_zero ());
	ASSERT_FALSE (init);
	celerix::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	celerix::raw_key prv1;
	wallet.fetch (transaction, key1.pub, prv1);
	ASSERT_EQ (key1.prv, prv1);
	ASSERT_FALSE (wallet.rekey (transaction, "1"));
	wallet.password.value (password);
	celerix::raw_key password1;
	wallet.derive_key (password1, transaction, "1");
	ASSERT_EQ (password1, password);
	celerix::raw_key prv2;
	wallet.fetch (transaction, key1.pub, prv2);
	ASSERT_EQ (key1.prv, prv2);
	*wallet.password.values[0] = 2;
	ASSERT_TRUE (wallet.rekey (transaction, "2"));
}

TEST (account, encode_zero)
{
	celerix::account number0{};
	std::string str0;
	number0.encode_account (str0);

	/*
	 * Handle different lengths for "xrb_" prefixed and "celerix_" prefixed accounts
	 */
	ASSERT_EQ ((str0.front () == 'x') ? 64 : 65, str0.size ());
	ASSERT_EQ (65, str0.size ());
	celerix::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_all)
{
	celerix::account number0;
	number0.decode_hex ("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
	std::string str0;
	number0.encode_account (str0);

	/*
	 * Handle different lengths for "xrb_" prefixed and "celerix_" prefixed accounts
	 */
	ASSERT_EQ ((str0.front () == 'x') ? 64 : 65, str0.size ());
	celerix::account number1;
	ASSERT_FALSE (number1.decode_account (str0));
	ASSERT_EQ (number0, number1);
}

TEST (account, encode_fail)
{
	celerix::account number0{};
	std::string str0;
	number0.encode_account (str0);
	str0[16] ^= 1;
	celerix::account number1;
	ASSERT_TRUE (number1.decode_account (str0));
}

TEST (wallet, hash_password)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (init);
	celerix::raw_key hash1;
	wallet.derive_key (hash1, transaction, "");
	celerix::raw_key hash2;
	wallet.derive_key (hash2, transaction, "");
	ASSERT_EQ (hash1, hash2);
	celerix::raw_key hash3;
	wallet.derive_key (hash3, transaction, "a");
	ASSERT_NE (hash1, hash3);
}

TEST (fan, reconstitute)
{
	celerix::raw_key value0 (0);
	celerix::fan fan (value0, 1024);
	for (auto & i : fan.values)
	{
		ASSERT_NE (value0, *i);
	}
	celerix::raw_key value1;
	fan.value (value1);
	ASSERT_EQ (value0, value1);
}

TEST (fan, change)
{
	celerix::raw_key value0;
	value0 = 0;
	celerix::raw_key value1;
	value1 = 1;
	ASSERT_NE (value0, value1);
	celerix::fan fan (value0, 1024);
	ASSERT_EQ (1024, fan.values.size ());
	celerix::raw_key value2;
	fan.value (value2);
	ASSERT_EQ (value0, value2);
	fan.value_set (value1);
	fan.value (value2);
	ASSERT_EQ (value1, value2);
}

TEST (wallet, reopen_default_password)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	auto transaction (env.tx_begin_write ());
	ASSERT_FALSE (init);
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	{
		celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
		ASSERT_FALSE (init);
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
		ASSERT_FALSE (init);
		wallet.rekey (transaction, "");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		bool init;
		celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
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
	celerix::store::lmdb::env env (error, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (celerix::dev::genesis_key.pub, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	celerix::keypair key;
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
	celerix::store::lmdb::env env (error, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet1 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (error);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	celerix::wallet_store wallet2 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "1", serialized);
	ASSERT_FALSE (error);
	celerix::raw_key password1;
	celerix::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_EQ (wallet1.end (transaction), wallet1.begin (transaction));
	ASSERT_EQ (wallet2.end (transaction), wallet2.begin (transaction));
}

TEST (wallet, serialize_json_one)
{
	auto error (false);
	celerix::store::lmdb::env env (error, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet1 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (error);
	celerix::keypair key;
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	celerix::wallet_store wallet2 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "1", serialized);
	ASSERT_FALSE (error);
	celerix::raw_key password1;
	celerix::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	celerix::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet, serialize_json_password)
{
	auto error (false);
	celerix::store::lmdb::env env (error, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet1 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (error);
	celerix::keypair key;
	wallet1.rekey (transaction, "password");
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	celerix::wallet_store wallet2 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "1", serialized);
	ASSERT_FALSE (error);
	ASSERT_FALSE (wallet2.valid_password (transaction));
	ASSERT_FALSE (wallet2.attempt_password (transaction, "password"));
	ASSERT_TRUE (wallet2.valid_password (transaction));
	celerix::raw_key password1;
	celerix::raw_key password2;
	wallet1.wallet_key (password1, transaction);
	wallet2.wallet_key (password2, transaction);
	ASSERT_EQ (password1, password2);
	ASSERT_EQ (wallet1.salt (transaction), wallet2.salt (transaction));
	ASSERT_EQ (wallet1.check (transaction), wallet2.check (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	celerix::raw_key prv;
	wallet2.fetch (transaction, key.pub, prv);
	ASSERT_EQ (key.prv, prv);
}

TEST (wallet_store, move)
{
	auto error (false);
	celerix::store::lmdb::env env (error, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (error);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet1 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (error);
	celerix::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	celerix::wallet_store wallet2 (error, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "1");
	ASSERT_FALSE (error);
	celerix::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
	std::vector<celerix::public_key> keys;
	keys.push_back (key2.pub);
	ASSERT_FALSE (wallet1.move (transaction, wallet2, keys));
	ASSERT_TRUE (wallet1.exists (transaction, key2.pub));
	ASSERT_FALSE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, import)
{
	celerix::test::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	celerix::keypair key1;
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
	celerix::test::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	celerix::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, fail_import_corrupt)
{
	celerix::test::system system (2);
	auto wallet1 (system.wallet (1));
	std::string json;
	auto error (wallet1->import (json, "1"));
	ASSERT_TRUE (error);
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
	celerix::test::system system (1);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (celerix::dev::genesis_key.prv);
	wallet->insert_adhoc (celerix::dev::genesis_key.prv);
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		uint64_t work (0);
		if (!wallet->store.work_get (transaction, celerix::dev::genesis_key.pub, work))
		{
			done = celerix::dev::network_params.work.difficulty (celerix::dev::genesis->work_version (), celerix::dev::genesis->hash (), work) >= system.nodes[0]->default_difficulty (celerix::dev::genesis->work_version ());
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_generate)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	celerix::uint128_t amount1 (node1.balance (celerix::dev::genesis_key.pub));
	uint64_t work1;
	wallet->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::account account1;
	{
		auto transaction = node1.wallets.tx_begin_read ();
		account1 = system.account (transaction, 0);
	}
	celerix::keypair key;
	auto block (wallet->send_action (celerix::dev::genesis_key.pub, key.pub, 100));
	ASSERT_TIMELY (10s, node1.ledger.any.account_balance (node1.ledger.tx_begin_read (), celerix::dev::genesis_key.pub) != amount1);
	system.deadline_set (10s);
	auto again (true);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto block_transaction = node1.ledger.tx_begin_read ();
		auto transaction (system.wallet (0)->wallets.tx_begin_read ());
		again = wallet->store.work_get (transaction, account1, work1) || celerix::dev::network_params.work.difficulty (block->work_version (), node1.ledger.latest_root (block_transaction, account1), work1) < node1.default_difficulty (block->work_version ());
	}
}

TEST (wallet, work_cache_delayed)
{
	celerix::test::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	uint64_t work1;
	wallet->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::account account1;
	{
		auto transaction = node1.wallets.tx_begin_read ();
		account1 = system.account (transaction, 0);
	}
	celerix::keypair key;
	auto block1 (wallet->send_action (celerix::dev::genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block1->hash (), node1.latest (celerix::dev::genesis_key.pub));
	auto block2 (wallet->send_action (celerix::dev::genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block2->hash (), node1.latest (celerix::dev::genesis_key.pub));
	ASSERT_EQ (block2->hash (), node1.wallets.delayed_work->operator[] (celerix::dev::genesis_key.pub).as_block_hash ());
	auto threshold (node1.default_difficulty (celerix::work_version::work_1));
	auto again (true);
	system.deadline_set (10s);
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		if (!wallet->store.work_get (node1.wallets.tx_begin_read (), account1, work1))
		{
			again = celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, block2->hash (), work1) < threshold;
		}
	}
	ASSERT_GE (celerix::dev::network_params.work.difficulty (celerix::work_version::work_1, block2->hash (), work1), threshold);
}

TEST (wallet, insert_locked)
{
	celerix::test::system system (1);
	auto wallet (system.wallet (0));
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->store.rekey (transaction, "1");
		ASSERT_TRUE (wallet->store.valid_password (transaction));
		wallet->enter_password (transaction, "");
	}
	auto transaction (wallet->wallets.tx_begin_read ());
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->insert_adhoc (celerix::keypair ().prv).is_zero ());
}

TEST (wallet, deterministic_keys)
{
	bool init;
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	auto key1 = wallet.deterministic_key (transaction, 0);
	auto key2 = wallet.deterministic_key (transaction, 0);
	ASSERT_EQ (key1, key2);
	auto key3 = wallet.deterministic_key (transaction, 1);
	ASSERT_NE (key1, key3);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	auto key4 (wallet.deterministic_insert (transaction));
	celerix::raw_key key5;
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
	celerix::raw_key key7;
	ASSERT_FALSE (wallet.fetch (transaction, key6, key7));
	ASSERT_NE (key5, key7);
	ASSERT_EQ (3, wallet.deterministic_index_get (transaction));
	celerix::keypair key9;
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
	celerix::store::lmdb::env env (init, celerix::unique_path () / "wallet.ldb");
	ASSERT_FALSE (init);
	auto transaction (env.tx_begin_write ());
	celerix::kdf kdf{ celerix::dev::network_params.kdf_work };
	celerix::wallet_store wallet (init, kdf, transaction, env, celerix::dev::genesis_key.pub, 1, "0");
	celerix::raw_key seed1;
	seed1 = 1;
	celerix::raw_key seed2;
	seed2 = 2;
	wallet.seed_set (transaction, seed1);
	celerix::raw_key seed3;
	wallet.seed (seed3, transaction);
	ASSERT_EQ (seed1, seed3);
	auto key1 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.seed_set (transaction, seed2);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	celerix::raw_key seed4;
	wallet.seed (seed4, transaction);
	ASSERT_EQ (seed2, seed4);
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_NE (key1, key2);
	wallet.seed_set (transaction, seed1);
	celerix::raw_key seed5;
	wallet.seed (seed5, transaction);
	ASSERT_EQ (seed1, seed5);
	auto key3 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key1, key3);
}

TEST (wallet, insert_deterministic_locked)
{
	celerix::test::system system (1);
	auto wallet (system.wallet (0));
	auto transaction (wallet->wallets.tx_begin_write ());
	wallet->store.rekey (transaction, "1");
	ASSERT_TRUE (wallet->store.valid_password (transaction));
	wallet->enter_password (transaction, "");
	ASSERT_FALSE (wallet->store.valid_password (transaction));
	ASSERT_TRUE (wallet->deterministic_insert (transaction).is_zero ());
}

TEST (wallet, no_work)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv, false);
	celerix::keypair key2;
	auto block (system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, std::numeric_limits<celerix::uint128_t>::max (), false));
	ASSERT_NE (nullptr, block);
	ASSERT_NE (0, block->block_work ());
	ASSERT_GE (celerix::dev::network_params.work.difficulty (*block), celerix::dev::network_params.work.threshold (block->work_version (), block->sideband ().details));
	auto transaction (system.wallet (0)->wallets.tx_begin_read ());
	uint64_t cached_work (0);
	system.wallet (0)->store.work_get (transaction, celerix::dev::genesis_key.pub, cached_work);
	ASSERT_EQ (0, cached_work);
}

TEST (wallet, send_race)
{
	celerix::test::system system (1);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::keypair key2;
	for (auto i (1); i < 60; ++i)
	{
		ASSERT_NE (nullptr, system.wallet (0)->send_action (celerix::dev::genesis_key.pub, key2.pub, celerix::Kcelerix_ratio));
		ASSERT_EQ (celerix::dev::constants.genesis_amount - celerix::Kcelerix_ratio * i, system.nodes[0]->balance (celerix::dev::genesis_key.pub));
	}
}

TEST (wallet, password_race)
{
	celerix::test::system system (1);
	celerix::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	std::thread thread ([&wallet] () {
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
	celerix::test::system system (1);
	celerix::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	celerix::raw_key seed;
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		ASSERT_FALSE (wallet->store.rekey (transaction, "4567"));
		wallet->store.seed (seed, transaction);
		ASSERT_FALSE (wallet->store.attempt_password (transaction, "4567"));
	}
	std::vector<std::thread> threads;
	for (int i = 0; i < 100; i++)
	{
		threads.emplace_back ([&wallet] () {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "0000");
			}
		});
		threads.emplace_back ([&wallet] () {
			for (int i = 0; i < 10; i++)
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				wallet->store.rekey (transaction, "1234");
			}
		});
		threads.emplace_back ([&wallet] () {
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
			celerix::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_EQ (seed_now, seed);
		}
		else if (!wallet->store.attempt_password (transaction, "0000"))
		{
			celerix::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_EQ (seed_now, seed);
		}
		else if (!wallet->store.attempt_password (transaction, "4567"))
		{
			celerix::raw_key seed_now;
			wallet->store.seed (seed_now, transaction);
			ASSERT_EQ (seed_now, seed);
		}
		else
		{
			ASSERT_FALSE (true);
		}
	}
}

TEST (wallet, change_seed)
{
	celerix::test::system system (1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	celerix::raw_key seed1;
	seed1 = 1;
	celerix::public_key pub;
	uint32_t index (4);
	auto prv = celerix::deterministic_key (seed1, index);
	pub = celerix::pub_key (prv);
	wallet->insert_adhoc (celerix::dev::genesis_key.prv, false);
	auto block (wallet->send_action (celerix::dev::genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (5s, celerix::test::exists (*system.nodes[0], { block }));
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		celerix::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, deterministic_restore)
{
	celerix::test::system system (1);
	auto wallet (system.wallet (0));
	wallet->enter_initial_password ();
	celerix::raw_key seed1;
	seed1 = 1;
	celerix::public_key pub;
	uint32_t index (4);
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->change_seed (transaction, seed1);
		celerix::raw_key seed2;
		wallet->store.seed (seed2, transaction);
		ASSERT_EQ (seed1, seed2);
		ASSERT_EQ (1, wallet->store.deterministic_index_get (transaction));
		auto prv = celerix::deterministic_key (seed1, index);
		pub = celerix::pub_key (prv);
	}
	wallet->insert_adhoc (celerix::dev::genesis_key.prv, false);
	auto block (wallet->send_action (celerix::dev::genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (5s, celerix::test::exists (*system.nodes[0], { block }));
	{
		auto transaction (wallet->wallets.tx_begin_write ());
		wallet->deterministic_restore (transaction);
		ASSERT_EQ (index + 1, wallet->store.deterministic_index_get (transaction));
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, epoch_2_validation)
{
	celerix::test::system system (1);
	auto & node (*system.nodes[0]);
	auto & wallet (*system.wallet (0));

	// Upgrade the genesis account to epoch 2
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, celerix::epoch::epoch_1));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, celerix::epoch::epoch_2));

	wallet.insert_adhoc (celerix::dev::genesis_key.prv, false);

	// Test send and receive blocks
	// An epoch 2 receive block should be generated with lower difficulty with high probability
	auto tries = 0;
	auto max_tries = 20;
	auto amount = node.config.receive_minimum.number ();
	while (++tries < max_tries)
	{
		auto send = wallet.send_action (celerix::dev::genesis_key.pub, celerix::dev::genesis_key.pub, amount, 1);
		ASSERT_NE (nullptr, send);
		ASSERT_EQ (celerix::epoch::epoch_2, send->sideband ().details.epoch);
		ASSERT_EQ (celerix::epoch::epoch_0, send->sideband ().source_epoch); // Not used for send state blocks

		auto receive = wallet.receive_action (send->hash (), celerix::dev::genesis_key.pub, amount, send->destination (), 1);
		ASSERT_NE (nullptr, receive);
		if (celerix::dev::network_params.work.difficulty (*receive) < node.network_params.work.base)
		{
			ASSERT_GE (celerix::dev::network_params.work.difficulty (*receive), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (celerix::epoch::epoch_2, receive->sideband ().details.epoch);
			ASSERT_EQ (celerix::epoch::epoch_2, receive->sideband ().source_epoch);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);

	// Test a change block
	ASSERT_NE (nullptr, wallet.change_action (celerix::dev::genesis_key.pub, celerix::keypair ().pub, 1));
}

// Receiving from an upgraded account uses the lower threshold and upgrades the receiving account
TEST (wallet, epoch_2_receive_propagation)
{
	auto tries = 0;
	auto const max_tries = 20;
	while (++tries < max_tries)
	{
		celerix::test::system system;
		celerix::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, celerix::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		celerix::keypair key;
		celerix::state_block_builder builder;

		// Send and open the account
		wallet.insert_adhoc (celerix::dev::genesis_key.prv, false);
		wallet.insert_adhoc (key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (celerix::dev::genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send1);
		ASSERT_NE (nullptr, wallet.receive_action (send1->hash (), celerix::dev::genesis_key.pub, amount, send1->destination (), 1));

		// Upgrade the genesis account to epoch 2
		auto epoch2 = system.upgrade_genesis_epoch (node, celerix::epoch::epoch_2);
		ASSERT_NE (nullptr, epoch2);

		// Send a block
		auto send2 = wallet.send_action (celerix::dev::genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send2);

		auto receive2 = wallet.receive_action (send2->hash (), key.pub, amount, send2->destination (), 1);
		ASSERT_NE (nullptr, receive2);
		if (celerix::dev::network_params.work.difficulty (*receive2) < node.network_params.work.base)
		{
			ASSERT_GE (celerix::dev::network_params.work.difficulty (*receive2), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (celerix::epoch::epoch_2, node.ledger.version (*receive2));
			ASSERT_EQ (celerix::epoch::epoch_2, receive2->sideband ().source_epoch);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);
}

// Opening an upgraded account uses the lower threshold
TEST (wallet, epoch_2_receive_unopened)
{
	// Ensure the lower receive work is used when receiving
	auto tries = 0;
	auto const max_tries = 20;
	while (++tries < max_tries)
	{
		celerix::test::system system;
		celerix::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, celerix::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		celerix::keypair key;
		celerix::state_block_builder builder;

		// Send
		wallet.insert_adhoc (celerix::dev::genesis_key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (celerix::dev::genesis_key.pub, key.pub, amount, 1);

		// Upgrade unopened account to epoch_2
		auto epoch2_unopened = builder
							   .account (key.pub)
							   .previous (0)
							   .representative (0)
							   .balance (0)
							   .link (node.network_params.ledger.epochs.link (celerix::epoch::epoch_2))
							   .sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
							   .work (*system.work.generate (key.pub, node.network_params.work.epoch_2))
							   .build ();
		ASSERT_EQ (celerix::block_status::progress, node.process (epoch2_unopened));

		wallet.insert_adhoc (key.prv, false);

		auto receive1 = wallet.receive_action (send1->hash (), key.pub, amount, send1->destination (), 1);
		ASSERT_NE (nullptr, receive1);
		if (celerix::dev::network_params.work.difficulty (*receive1) < node.network_params.work.base)
		{
			ASSERT_GE (celerix::dev::network_params.work.difficulty (*receive1), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (celerix::epoch::epoch_2, node.ledger.version (*receive1));
			ASSERT_EQ (celerix::epoch::epoch_1, receive1->sideband ().source_epoch);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);
}

/**
 * This test checks that wallets::foreach_representative can be used recursively
 */
TEST (wallet, foreach_representative_deadlock)
{
	celerix::test::system system (1);
	auto & node (*system.nodes[0]);
	system.wallet (0)->insert_adhoc (celerix::dev::genesis_key.prv);
	node.wallets.compute_reps ();
	ASSERT_EQ (1, node.wallets.reps ().voting);

	bool set = false;
	node.wallets.foreach_representative ([&node, &set, &system] (celerix::public_key const & pub, celerix::raw_key const & prv) {
		node.wallets.foreach_representative ([&node, &set, &system] (celerix::public_key const & pub, celerix::raw_key const & prv) {
			ASSERT_TIMELY (5s, node.wallets.mutex.try_lock () == 1);
			node.wallets.mutex.unlock ();
			set = true;
		});
	});
	ASSERT_TRUE (set);
}

TEST (wallet, search_receivable)
{
	celerix::test::system system;
	celerix::node_config config = system.default_config ();
	config.enable_voting = false;
	config.backlog_scan.enable = false;
	celerix::node_flags flags;
	flags.disable_search_pending = true;
	auto & node (*system.add_node (config, flags));
	auto & wallet (*system.wallet (0));

	wallet.insert_adhoc (celerix::dev::genesis_key.prv);
	celerix::block_builder builder;
	auto send = builder.state ()
				.account (celerix::dev::genesis_key.pub)
				.previous (celerix::dev::genesis->hash ())
				.representative (celerix::dev::genesis_key.pub)
				.balance (celerix::dev::constants.genesis_amount - node.config.receive_minimum.number ())
				.link (celerix::dev::genesis_key.pub)
				.sign (celerix::dev::genesis_key.prv, celerix::dev::genesis_key.pub)
				.work (*system.work.generate (celerix::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (celerix::block_status::progress, node.process (send));

	// Pending search should start an election
	ASSERT_TRUE (node.active.empty ());
	ASSERT_FALSE (wallet.search_receivable (wallet.wallets.tx_begin_read ()));
	std::shared_ptr<celerix::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send->qualified_root ()));

	// Erase the key so the confirmation does not trigger an automatic receive
	wallet.store.erase (node.wallets.tx_begin_write (), celerix::dev::genesis_key.pub);

	// Now confirm the election
	election->force_confirm ();

	ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()) && node.active.empty ());

	// Re-insert the key
	wallet.insert_adhoc (celerix::dev::genesis_key.prv);

	// Pending search should create the receive block
	ASSERT_EQ (2, node.ledger.block_count ());
	ASSERT_FALSE (wallet.search_receivable (wallet.wallets.tx_begin_read ()));
	ASSERT_TIMELY_EQ (3s, node.balance (celerix::dev::genesis_key.pub), celerix::dev::constants.genesis_amount);
	auto receive_hash = node.ledger.any.account_head (node.ledger.tx_begin_read (), celerix::dev::genesis_key.pub);
	auto receive = node.block (receive_hash);
	ASSERT_NE (nullptr, receive);
	ASSERT_EQ (receive->sideband ().height, 3);
	ASSERT_EQ (send->hash (), receive->source ());
}

TEST (wallet, receive_pruned)
{
	celerix::test::system system;
	celerix::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node1 = *system.add_node (node_flags);
	node_flags.enable_pruning = true;
	celerix::node_config config = system.default_config ();
	config.enable_voting = false; // Remove after allowing pruned voting
	auto & node2 = *system.add_node (config, node_flags);

	auto & wallet1 = *system.wallet (0);
	auto & wallet2 = *system.wallet (1);

	celerix::keypair key;
	celerix::state_block_builder builder;

	// Send
	wallet1.insert_adhoc (celerix::dev::genesis_key.prv, false);
	auto amount = node2.config.receive_minimum.number ();
	auto send1 = wallet1.send_action (celerix::dev::genesis_key.pub, key.pub, amount, 1);
	auto send2 = wallet1.send_action (celerix::dev::genesis_key.pub, key.pub, 1, 1);

	// Pruning
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 3);
	{
		auto transaction = node2.ledger.tx_begin_write ();
		ASSERT_EQ (1, node2.ledger.pruning_action (transaction, send1->hash (), 2));
	}
	ASSERT_EQ (1, node2.ledger.pruned_count ());
	ASSERT_TRUE (node2.block_or_pruned_exists (send1->hash ()));
	ASSERT_FALSE (node2.ledger.any.block_exists (node2.ledger.tx_begin_read (), send1->hash ()));

	wallet2.insert_adhoc (key.prv, false);

	auto open1 = wallet2.receive_action (send1->hash (), key.pub, amount, send1->destination (), 1);
	ASSERT_NE (nullptr, open1);
	ASSERT_EQ (amount, node2.ledger.any.block_balance (node2.ledger.tx_begin_read (), open1->hash ()));
	ASSERT_TIMELY_EQ (5s, node2.ledger.cemented_count (), 4);
}
