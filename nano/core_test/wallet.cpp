#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/work_version.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/election.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/wallet.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>
#include <nano/wallet/lmdb/wallets_backend_lmdb.hpp>
#include <nano/wallet/wallet_value.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/*
 * fan
 */

TEST (fan, reconstitute)
{
	nano::raw_key value0 (0);
	nano::fan fan (value0, 1024);
	for (auto & i : fan.values)
	{
		ASSERT_NE (value0, *i);
	}
	nano::raw_key value1;
	fan.value (value1);
	ASSERT_EQ (value0, value1);
}

TEST (fan, change)
{
	nano::raw_key value0;
	value0 = 0;
	nano::raw_key value1;
	value1 = 1;
	ASSERT_NE (value0, value1);
	nano::fan fan (value0, 1024);
	ASSERT_EQ (1024, fan.values.size ());
	nano::raw_key value2;
	fan.value (value2);
	ASSERT_EQ (value0, value2);
	fan.value_set (value1);
	fan.value (value2);
	ASSERT_EQ (value1, value2);
}

/*
 * wallet_store
 */

TEST (wallet_store, no_special_keys_accounts)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));

	for (uint64_t account = 0; account < nano::wallet::wallet_store::special_count; account++)
	{
		nano::account account_l (account);
		ASSERT_FALSE (wallet.exists (transaction, account_l));
	}
}

TEST (wallet_store, no_key)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	auto result = wallet.fetch (transaction, key1.pub);
	ASSERT_FALSE (result);
	ASSERT_EQ (result.error (), nano::error_common::account_not_found_wallet);
	ASSERT_TRUE (wallet.valid_password (transaction));
}

TEST (wallet_store, fetch_locked)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	ASSERT_TRUE (wallet.valid_password (transaction));
	nano::keypair key1;
	ASSERT_EQ (key1.pub, wallet.insert_adhoc (transaction, key1.prv));
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_FALSE (key2.is_zero ());
	nano::raw_key key3;
	key3 = 1;
	wallet.password.value_set (key3);
	ASSERT_FALSE (wallet.valid_password (transaction));
	auto result1 = wallet.fetch (transaction, key1.pub);
	ASSERT_FALSE (result1);
	ASSERT_EQ (result1.error (), nano::error_common::wallet_locked);
	auto result2 = wallet.fetch (transaction, key2);
	ASSERT_FALSE (result2);
	ASSERT_EQ (result2.error (), nano::error_common::wallet_locked);
}

TEST (wallet_store, retrieval)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	ASSERT_TRUE (wallet.valid_password (transaction));
	wallet.insert_adhoc (transaction, key1.prv);
	auto result1 = wallet.fetch (transaction, key1.pub);
	ASSERT_TRUE (result1);
	ASSERT_TRUE (wallet.valid_password (transaction));
	ASSERT_EQ (key1.prv, result1.value ());
	wallet.password.values[0]->bytes[16] ^= 1;
	auto result2 = wallet.fetch (transaction, key1.pub);
	ASSERT_FALSE (result2);
	ASSERT_EQ (result2.error (), nano::error_common::wallet_locked);
	ASSERT_FALSE (wallet.valid_password (transaction));
}

TEST (wallet_store, unlock_locked)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	// Fresh wallet unlocks under the default password
	auto cipher_unlocked = wallet.unlock (transaction);
	ASSERT_TRUE (cipher_unlocked);
	// Corrupt the live password fan to simulate a locked / wrong-password state
	nano::raw_key garbage;
	garbage = 1;
	wallet.password.value_set (garbage);
	auto cipher_locked = wallet.unlock (transaction);
	ASSERT_FALSE (cipher_locked);
}

TEST (wallet_store, cipher_round_trip)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	auto cipher = wallet.unlock (transaction);
	ASSERT_TRUE (cipher);
	// Random non-zero plaintext: covers the non-zero encrypt case
	nano::raw_key plaintext;
	nano::random_pool::generate_block (plaintext.bytes.data (), plaintext.bytes.size ());
	ASSERT_FALSE (plaintext.is_zero ());
	nano::uint128_union iv{ 42 };
	auto ciphertext = cipher.value ().encrypt (plaintext, iv);
	// Encryption should actually transform the data
	ASSERT_NE (plaintext, ciphertext);
	// Round-trip: decrypt with the same IV recovers the plaintext
	auto recovered = cipher.value ().decrypt (ciphertext, iv);
	ASSERT_EQ (plaintext, recovered);
	// Sanity: a different IV does not recover the plaintext
	nano::uint128_union iv_other{ 43 };
	ASSERT_NE (plaintext, cipher.value ().decrypt (ciphertext, iv_other));
	// Encrypting different plaintexts under the same IV produces different ciphertexts
	nano::raw_key plaintext_other;
	nano::random_pool::generate_block (plaintext_other.bytes.data (), plaintext_other.bytes.size ());
	auto ciphertext_other = cipher.value ().encrypt (plaintext_other, iv);
	ASSERT_NE (ciphertext, ciphertext_other);
}

TEST (wallet_store, empty_iteration)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	auto i (wallet.begin (transaction));
	auto j (wallet.end (transaction));
	ASSERT_EQ (i, j);
}

TEST (wallet_store, one_item_iteration)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	for (auto i (wallet.begin (transaction)), j (wallet.end (transaction)); i != j; ++i)
	{
		ASSERT_EQ (key1.pub, nano::uint256_union (i->first));
		auto cipher = wallet.unlock (transaction);
		ASSERT_TRUE (cipher);
		auto key = cipher.value ().decrypt (nano::wallet::wallet_value (i->second).key, (nano::uint256_union (i->first)).owords[0]);
		ASSERT_EQ (key1.prv, key);
	}
}

TEST (wallet_store, two_item_iteration)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	nano::keypair key1;
	nano::keypair key2;
	ASSERT_NE (key1.pub, key2.pub);
	std::unordered_set<nano::public_key> pubs;
	std::unordered_set<nano::raw_key> prvs;
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	{
		auto transaction (backend.tx_begin_write ());
		nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
		wallet.insert_adhoc (transaction, key1.prv);
		wallet.insert_adhoc (transaction, key2.prv);
		for (auto i (wallet.begin (transaction)), j (wallet.end (transaction)); i != j; ++i)
		{
			pubs.insert (i->first);
			auto cipher = wallet.unlock (transaction);
			ASSERT_TRUE (cipher);
			auto key = cipher.value ().decrypt (nano::wallet::wallet_value (i->second).key, (i->first).owords[0]);
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

TEST (wallet_store, find_none)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::account account (1000);
	ASSERT_EQ (wallet.end (transaction), wallet.find (transaction, account));
}

TEST (wallet_store, find_existing)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	ASSERT_FALSE (wallet.exists (transaction, key1.pub));
	wallet.insert_adhoc (transaction, key1.prv);
	ASSERT_TRUE (wallet.exists (transaction, key1.pub));
	auto existing (wallet.find (transaction, key1.pub));
	ASSERT_NE (wallet.end (transaction), existing);
	++existing;
	ASSERT_EQ (wallet.end (transaction), existing);
}

TEST (wallet_store, rekey)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::raw_key password;
	wallet.password.value (password);
	auto default_password_key = wallet.derive_key (transaction, nano::wallet::wallet_store::default_password);
	ASSERT_EQ (default_password_key, password);
	nano::keypair key1;
	wallet.insert_adhoc (transaction, key1.prv);
	auto result1 = wallet.fetch (transaction, key1.pub);
	ASSERT_TRUE (result1);
	ASSERT_EQ (key1.prv, result1.value ());
	ASSERT_FALSE (wallet.rekey (transaction, "1"));
	wallet.password.value (password);
	auto password1 = wallet.derive_key (transaction, "1");
	ASSERT_EQ (password1, password);
	auto result2 = wallet.fetch (transaction, key1.pub);
	ASSERT_TRUE (result2);
	ASSERT_EQ (key1.prv, result2.value ());
	*wallet.password.values[0] = 2;
	ASSERT_TRUE (wallet.rekey (transaction, "2"));
}

TEST (wallet_store, hash_password)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	auto hash1 = wallet.derive_key (transaction, "");
	auto hash2 = wallet.derive_key (transaction, "");
	ASSERT_EQ (hash1, hash2);
	auto hash3 = wallet.derive_key (transaction, "a");
	ASSERT_NE (hash1, hash3);
}

TEST (wallet_store, reopen_default_password)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	{
		nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		// Rekey to a non-default password
		nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
		wallet.rekey (transaction, "secret");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
	{
		// Default password no longer works after rekey
		nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, "");
		ASSERT_FALSE (wallet.valid_password (transaction));
		wallet.attempt_password (transaction, "secret");
		ASSERT_TRUE (wallet.valid_password (transaction));
	}
}

TEST (wallet_store, representative)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (nano::dev::genesis_key.pub, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	nano::keypair key;
	wallet.representative_set (transaction, key.pub);
	ASSERT_FALSE (wallet.is_representative (transaction));
	ASSERT_EQ (key.pub, wallet.representative (transaction));
	ASSERT_FALSE (wallet.is_representative (transaction));
	wallet.insert_adhoc (transaction, key.prv);
	ASSERT_TRUE (wallet.is_representative (transaction));
}

TEST (wallet_store, serialize_json_empty)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, 1, "1", serialized);
	auto cipher1 = wallet1.unlock (transaction);
	auto cipher2 = wallet2.unlock (transaction);
	ASSERT_TRUE (cipher1);
	ASSERT_TRUE (cipher2);
	nano::raw_key zero;
	zero.clear ();
	nano::uint128_union iv{ 0 };
	ASSERT_EQ (cipher1.value ().encrypt (zero, iv), cipher2.value ().encrypt (zero, iv));
	ASSERT_EQ (wallet1.salt_get (transaction), wallet2.salt_get (transaction));
	ASSERT_EQ (wallet1.check_value_get (transaction), wallet2.check_value_get (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_EQ (wallet1.end (transaction), wallet1.begin (transaction));
	ASSERT_EQ (wallet2.end (transaction), wallet2.begin (transaction));
}

TEST (wallet_store, serialize_json_one)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key;
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, 1, "1", serialized);
	auto cipher1 = wallet1.unlock (transaction);
	auto cipher2 = wallet2.unlock (transaction);
	ASSERT_TRUE (cipher1);
	ASSERT_TRUE (cipher2);
	nano::raw_key zero;
	zero.clear ();
	nano::uint128_union iv{ 0 };
	ASSERT_EQ (cipher1.value ().encrypt (zero, iv), cipher2.value ().encrypt (zero, iv));
	ASSERT_EQ (wallet1.salt_get (transaction), wallet2.salt_get (transaction));
	ASSERT_EQ (wallet1.check_value_get (transaction), wallet2.check_value_get (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	auto prv_result = wallet2.fetch (transaction, key.pub);
	ASSERT_TRUE (prv_result);
	ASSERT_EQ (key.prv, prv_result.value ());
}

TEST (wallet_store, serialize_json_password)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key;
	wallet1.rekey (transaction, "password");
	wallet1.insert_adhoc (transaction, key.prv);
	std::string serialized;
	wallet1.serialize_json (transaction, serialized);
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, 1, "1", serialized);
	ASSERT_FALSE (wallet2.valid_password (transaction));
	ASSERT_FALSE (wallet2.attempt_password (transaction, "password"));
	ASSERT_TRUE (wallet2.valid_password (transaction));
	auto cipher1 = wallet1.unlock (transaction);
	auto cipher2 = wallet2.unlock (transaction);
	ASSERT_TRUE (cipher1);
	ASSERT_TRUE (cipher2);
	nano::raw_key zero;
	zero.clear ();
	nano::uint128_union iv{ 0 };
	ASSERT_EQ (cipher1.value ().encrypt (zero, iv), cipher2.value ().encrypt (zero, iv));
	ASSERT_EQ (wallet1.salt_get (transaction), wallet2.salt_get (transaction));
	ASSERT_EQ (wallet1.check_value_get (transaction), wallet2.check_value_get (transaction));
	ASSERT_EQ (wallet1.representative (transaction), wallet2.representative (transaction));
	ASSERT_TRUE (wallet2.exists (transaction, key.pub));
	auto prv_result = wallet2.fetch (transaction, key.pub);
	ASSERT_TRUE (prv_result);
	ASSERT_EQ (key.prv, prv_result.value ());
}

TEST (wallet_store, move)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "1");
	nano::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
	std::vector<nano::public_key> keys;
	keys.push_back (key2.pub);
	auto move_result = wallet1.move (transaction, wallet2, keys);
	ASSERT_TRUE (move_result);
	ASSERT_FALSE (move_result.value ());
	ASSERT_TRUE (wallet1.exists (transaction, key2.pub));
	ASSERT_FALSE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, move_locked)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::keypair key1;
	wallet1.insert_adhoc (transaction, key1.prv);
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "1");
	nano::keypair key2;
	wallet2.insert_adhoc (transaction, key2.prv);
	std::vector<nano::public_key> keys;
	keys.push_back (key2.pub);

	// Lock destination wallet
	nano::raw_key bad_password;
	bad_password = 1;
	wallet1.password.value_set (bad_password);
	ASSERT_FALSE (wallet1.valid_password (transaction));

	auto result1 = wallet1.move (transaction, wallet2, keys);
	ASSERT_FALSE (result1);
	ASSERT_EQ (result1.error (), nano::error_common::wallet_locked);
	// Key should not have been moved
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));

	// Unlock destination, lock source
	ASSERT_FALSE (wallet1.attempt_password (transaction, ""));
	ASSERT_TRUE (wallet1.valid_password (transaction));
	wallet2.password.value_set (bad_password);
	ASSERT_FALSE (wallet2.valid_password (transaction));

	auto result2 = wallet1.move (transaction, wallet2, keys);
	ASSERT_FALSE (result2);
	ASSERT_EQ (result2.error (), nano::error_common::wallet_locked);
	ASSERT_FALSE (wallet1.exists (transaction, key2.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key2.pub));
}

TEST (wallet_store, import_json)
{
	nano::test::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	nano::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize_json (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, ""));
	ASSERT_FALSE (error);
	ASSERT_TRUE (wallet2->exists (key1.pub));
}

TEST (wallet_store, import_locked)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet1 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::wallet::wallet_store wallet2 (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "1");
	nano::keypair key1;
	wallet2.insert_adhoc (transaction, key1.prv);

	// Lock destination wallet
	nano::raw_key bad_password;
	bad_password = 1;
	wallet1.password.value_set (bad_password);
	ASSERT_FALSE (wallet1.valid_password (transaction));

	auto result1 = wallet1.import (transaction, wallet2);
	ASSERT_FALSE (result1);
	ASSERT_EQ (result1.error (), nano::error_common::wallet_locked);
	// Key should not have been moved
	ASSERT_FALSE (wallet1.exists (transaction, key1.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key1.pub));

	// Unlock destination, lock source
	ASSERT_FALSE (wallet1.attempt_password (transaction, ""));
	ASSERT_TRUE (wallet1.valid_password (transaction));
	wallet2.password.value_set (bad_password);
	ASSERT_FALSE (wallet2.valid_password (transaction));

	auto result2 = wallet1.import (transaction, wallet2);
	ASSERT_FALSE (result2);
	ASSERT_EQ (result2.error (), nano::error_common::wallet_locked);
	ASSERT_FALSE (wallet1.exists (transaction, key1.pub));
	ASSERT_TRUE (wallet2.exists (transaction, key1.pub));
}

TEST (wallet_store, fail_import_bad_password)
{
	nano::test::system system (2);
	auto wallet1 (system.wallet (0));
	auto wallet2 (system.wallet (1));
	nano::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	std::string json;
	wallet1->serialize_json (json);
	ASSERT_FALSE (wallet2->exists (key1.pub));
	auto error (wallet2->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, fail_import_corrupt)
{
	nano::test::system system (2);
	auto wallet1 (system.wallet (1));
	std::string json;
	auto error (wallet1->import (json, "1"));
	ASSERT_TRUE (error);
}

TEST (wallet_store, deterministic_keys)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	auto key1 = wallet.deterministic_key (transaction, 0);
	auto key2 = wallet.deterministic_key (transaction, 0);
	ASSERT_EQ (key1, key2);
	auto key3 = wallet.deterministic_key (transaction, 1);
	ASSERT_NE (key1, key3);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	auto key4 (wallet.deterministic_insert (transaction));
	auto key5_result = wallet.fetch (transaction, key4);
	ASSERT_TRUE (key5_result);
	ASSERT_EQ (key3, key5_result.value ());
	ASSERT_EQ (2, wallet.deterministic_index_get (transaction));
	wallet.deterministic_index_set (transaction, 1);
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.erase (transaction, key4);
	ASSERT_FALSE (wallet.exists (transaction, key4));
	auto key8 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key4, key8);
	auto key6 (wallet.deterministic_insert (transaction));
	auto key7_result = wallet.fetch (transaction, key6);
	ASSERT_TRUE (key7_result);
	ASSERT_NE (key5_result.value (), key7_result.value ());
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

TEST (wallet_store, reseed)
{
	nano::wallet::lmdb::wallets_backend_lmdb backend (nano::unique_path () / "wallet.ldb");
	auto transaction (backend.tx_begin_write ());
	nano::kdf kdf{ nano::dev::network_params.kdf_work };
	nano::wallet::wallet_store wallet (kdf, transaction, backend, nano::dev::genesis_key.pub, 1, "0");
	nano::raw_key seed1;
	seed1 = 1;
	nano::raw_key seed2;
	seed2 = 2;
	wallet.seed_set (transaction, seed1);
	auto seed3 = wallet.seed (transaction);
	ASSERT_EQ (seed1, seed3);
	auto key1 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (1, wallet.deterministic_index_get (transaction));
	wallet.seed_set (transaction, seed2);
	ASSERT_EQ (0, wallet.deterministic_index_get (transaction));
	auto seed4 = wallet.seed (transaction);
	ASSERT_EQ (seed2, seed4);
	auto key2 (wallet.deterministic_insert (transaction));
	ASSERT_NE (key1, key2);
	wallet.seed_set (transaction, seed1);
	auto seed5 = wallet.seed (transaction);
	ASSERT_EQ (seed1, seed5);
	auto key3 (wallet.deterministic_insert (transaction));
	ASSERT_EQ (key1, key3);
}

/*
 * wallet
 */

TEST (wallet, insufficient_spend_one)
{
	nano::test::system system (1);
	nano::keypair key1;
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	auto block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, 500));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key1.pub, nano::dev::constants.genesis_amount));
}

TEST (wallet, spend_all_one)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max ()));
	auto transaction = node1.ledger.tx_begin_read ();
	auto info2 = node1.ledger.any.account_get (transaction, nano::dev::genesis_key.pub);
	ASSERT_NE (latest1, info2->head);
	auto block = node1.ledger.any.block_get (transaction, info2->head);
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (latest1, block->previous ());
	ASSERT_TRUE (info2->balance.is_zero ());
	ASSERT_EQ (0, node1.balance (nano::dev::genesis_key.pub));
}

TEST (wallet, send_async)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	std::thread thread ([&system] () {
		ASSERT_TIMELY (10s, system.nodes[0]->balance (nano::dev::genesis_key.pub).is_zero ());
	});
	std::atomic<bool> success (false);
	system.wallet (0)->send_async (nano::dev::genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max (), [&success] (std::shared_ptr<nano::block> const & block_a) { ASSERT_NE (nullptr, block_a); success = true; });
	thread.join ();
	ASSERT_TIMELY (2s, success);
}

TEST (wallet, spend)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	nano::block_hash latest1 (node1.latest (nano::dev::genesis_key.pub));
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	// Sending from empty accounts should always be an error.  Accounts need to be opened with an open block, not a send block.
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (0, key2.pub, 0));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max ()));
	auto transaction = node1.ledger.tx_begin_read ();
	auto info2 = node1.ledger.any.account_get (transaction, nano::dev::genesis_key.pub);
	ASSERT_TRUE (info2);
	ASSERT_NE (latest1, info2->head);
	auto block = node1.ledger.any.block_get (transaction, info2->head);
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (latest1, block->previous ());
	ASSERT_TRUE (info2->balance.is_zero ());
	ASSERT_EQ (0, node1.balance (nano::dev::genesis_key.pub));
}

TEST (wallet, partial_spend)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - 500, system.nodes[0]->balance (nano::dev::genesis_key.pub));
}

TEST (wallet, spend_no_previous)
{
	nano::test::system system (1);
	{
		system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
		auto transaction = system.nodes[0]->ledger.tx_begin_read ();
		auto info1 = system.nodes[0]->ledger.any.account_get (transaction, nano::dev::genesis_key.pub);
		ASSERT_TRUE (info1);
		for (auto i (0); i < 50; ++i)
		{
			nano::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);
		}
	}
	nano::keypair key2;
	ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, 500));
	ASSERT_EQ (std::numeric_limits<nano::uint128_t>::max () - 500, system.nodes[0]->balance (nano::dev::genesis_key.pub));
}

// Test work is precached when a key is inserted
TEST (wallet, work)
{
	nano::test::system system (1);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto work_result = wallet->get_work (nano::dev::genesis_key.pub);
		if (work_result)
		{
			done = nano::dev::network_params.work.difficulty (nano::dev::genesis->work_version (), nano::dev::genesis->hash (), work_result.value ()) >= system.nodes[0]->default_difficulty (nano::dev::genesis->work_version ());
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (wallet, work_generate)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	nano::uint128_t amount1 (node1.balance (nano::dev::genesis_key.pub));
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	nano::account account1 = system.wallet (0)->accounts ().front ();
	nano::keypair key;
	auto block (wallet->send_action (nano::dev::genesis_key.pub, key.pub, 100));
	ASSERT_TIMELY (10s, node1.ledger.any.account_balance (node1.ledger.tx_begin_read (), nano::dev::genesis_key.pub) != amount1);
	system.deadline_set (10s);
	auto again = true;
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto block_transaction = node1.ledger.tx_begin_read ();
		auto work_result = wallet->get_work (account1);
		again = !work_result || nano::dev::network_params.work.difficulty (block->work_version (), node1.ledger.latest_root (block_transaction, account1), work_result.value ()) < node1.default_difficulty (block->work_version ());
	}
}

TEST (wallet, work_cache_delayed)
{
	nano::test::system system (1);
	auto & node1 (*system.nodes[0]);
	auto wallet (system.wallet (0));
	wallet->insert_adhoc (nano::dev::genesis_key.prv);
	nano::account account1 = system.wallet (0)->accounts ().front ();
	nano::keypair key;
	auto block1 (wallet->send_action (nano::dev::genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block1->hash (), node1.latest (nano::dev::genesis_key.pub));
	auto block2 (wallet->send_action (nano::dev::genesis_key.pub, key.pub, 100));
	ASSERT_EQ (block2->hash (), node1.latest (nano::dev::genesis_key.pub));
	ASSERT_EQ (block2->hash (), node1.wallets.delayed_work->operator[] (nano::dev::genesis_key.pub).as_block_hash ());
	auto threshold (node1.default_difficulty (nano::work_version::work_1));
	nano::result<uint64_t> work_result = nano::error (nano::error_common::account_not_found_wallet);
	system.deadline_set (10s);
	auto again = true;
	while (again)
	{
		ASSERT_NO_ERROR (system.poll ());
		work_result = wallet->get_work (account1);
		if (work_result)
		{
			again = nano::dev::network_params.work.difficulty (nano::work_version::work_1, block2->hash (), work_result.value ()) < threshold;
		}
	}
	ASSERT_TRUE (work_result);
	ASSERT_GE (nano::dev::network_params.work.difficulty (nano::work_version::work_1, block2->hash (), work_result.value ()), threshold);
}

TEST (wallet, insert_locked)
{
	nano::test::system system (1);
	auto wallet (system.wallet (0));
	{
		wallet->rekey ("1");
		ASSERT_FALSE (wallet->is_locked ());
		wallet->enter_password ("");
	}
	ASSERT_TRUE (wallet->is_locked ());
	auto insert_result = wallet->insert_adhoc (nano::keypair ().prv);
	ASSERT_FALSE (insert_result);
	ASSERT_EQ (insert_result.error (), nano::error_common::wallet_locked);
}

TEST (wallet, insert_deterministic_locked)
{
	nano::test::system system (1);
	auto wallet (system.wallet (0));
	wallet->rekey ("1");
	ASSERT_FALSE (wallet->is_locked ());
	wallet->enter_password ("");
	ASSERT_TRUE (wallet->is_locked ());
	auto insert_result = wallet->deterministic_insert ();
	ASSERT_FALSE (insert_result);
	ASSERT_EQ (insert_result.error (), nano::error_common::wallet_locked);
}

TEST (wallet, move_accounts)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto wallet1 (node.wallets.create (nano::random_wallet_id ()));
	auto wallet2 (node.wallets.create (nano::random_wallet_id ()));
	nano::keypair key1, key2;
	wallet2->insert_adhoc (key1.prv);
	wallet2->insert_adhoc (key2.prv);
	ASSERT_FALSE (wallet1->exists (key1.pub));
	ASSERT_FALSE (wallet1->exists (key2.pub));
	ASSERT_TRUE (wallet2->exists (key1.pub));
	ASSERT_TRUE (wallet2->exists (key2.pub));
	auto result = wallet1->move_accounts (*wallet2, { key1.pub, key2.pub });
	ASSERT_TRUE (result);
	ASSERT_FALSE (result.value ());
	ASSERT_TRUE (wallet1->exists (key1.pub));
	ASSERT_TRUE (wallet1->exists (key2.pub));
	ASSERT_FALSE (wallet2->exists (key1.pub));
	ASSERT_FALSE (wallet2->exists (key2.pub));
	// Verify keys are the same after move
	auto prv1 = wallet1->fetch_prv (key1.pub);
	ASSERT_TRUE (prv1);
	ASSERT_EQ (key1.prv, prv1.value ());
	auto prv2 = wallet1->fetch_prv (key2.pub);
	ASSERT_TRUE (prv2);
	ASSERT_EQ (key2.prv, prv2.value ());
}

TEST (wallet, move_accounts_locked)
{
	nano::test::system system (1);
	auto & node = *system.nodes[0];
	auto wallet1 (node.wallets.create (nano::random_wallet_id ()));
	auto wallet2 (node.wallets.create (nano::random_wallet_id ()));
	nano::keypair key1;
	wallet2->insert_adhoc (key1.prv);

	// Lock destination wallet
	wallet1->rekey ("1");
	wallet1->enter_password ("");
	ASSERT_TRUE (wallet1->is_locked ());

	auto result = wallet1->move_accounts (*wallet2, { key1.pub });
	ASSERT_FALSE (result);
	ASSERT_EQ (result.error (), nano::error_common::wallet_locked);
	// Key should not have been moved
	ASSERT_FALSE (wallet1->exists (key1.pub));
	ASSERT_TRUE (wallet2->exists (key1.pub));
}

TEST (wallet, no_work)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv, false);
	nano::keypair key2;
	auto block (system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, std::numeric_limits<nano::uint128_t>::max (), false));
	ASSERT_NE (nullptr, block);
	ASSERT_NE (0, block->block_work ());
	ASSERT_GE (nano::dev::network_params.work.difficulty (*block), nano::dev::network_params.work.threshold (block->work_version (), block->sideband ().details));
	auto cached_work_result = system.wallet (0)->get_work (nano::dev::genesis_key.pub);
	ASSERT_TRUE (cached_work_result);
	ASSERT_EQ (0, cached_work_result.value ());
}

TEST (wallet, send_race)
{
	nano::test::system system (1);
	system.wallet (0)->insert_adhoc (nano::dev::genesis_key.prv);
	nano::keypair key2;
	for (auto i (1); i < 60; ++i)
	{
		ASSERT_NE (nullptr, system.wallet (0)->send_action (nano::dev::genesis_key.pub, key2.pub, nano::Knano_ratio));
		ASSERT_EQ (nano::dev::constants.genesis_amount - nano::Knano_ratio * i, system.nodes[0]->balance (nano::dev::genesis_key.pub));
	}
}

TEST (wallet, password_race)
{
	nano::test::system system (1);
	nano::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	std::thread thread ([&wallet] () {
		for (int i = 0; i < 100; i++)
		{
			wallet->rekey (std::to_string (i));
		}
	});
	for (int i = 0; i < 100; i++)
	{
		// Password should always be valid, the rekey operation should be atomic.
		bool ok = !wallet->is_locked ();
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
	nano::test::system system (1);
	nano::thread_runner runner (system.io_ctx, system.logger, system.nodes[0]->config.io_threads);
	auto wallet = system.wallet (0);
	nano::raw_key seed;
	{
		ASSERT_FALSE (wallet->rekey ("4567"));
		auto seed_result = wallet->get_seed ();
		ASSERT_TRUE (seed_result);
		seed = seed_result.value ();
		ASSERT_FALSE (wallet->enter_password ("4567"));
	}
	std::vector<std::thread> threads;
	for (int i = 0; i < 100; i++)
	{
		threads.emplace_back ([&wallet] () {
			for (int i = 0; i < 10; i++)
			{
				wallet->rekey ("0000");
			}
		});
		threads.emplace_back ([&wallet] () {
			for (int i = 0; i < 10; i++)
			{
				wallet->rekey ("1234");
			}
		});
		threads.emplace_back ([&wallet] () {
			for (int i = 0; i < 10; i++)
			{
				wallet->enter_password ("1234");
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
		if (!wallet->enter_password ("1234"))
		{
			auto seed_now = wallet->get_seed ();
			ASSERT_TRUE (seed_now);
			ASSERT_EQ (seed_now.value (), seed);
		}
		else if (!wallet->enter_password ("0000"))
		{
			auto seed_now = wallet->get_seed ();
			ASSERT_TRUE (seed_now);
			ASSERT_EQ (seed_now.value (), seed);
		}
		else if (!wallet->enter_password ("4567"))
		{
			auto seed_now = wallet->get_seed ();
			ASSERT_TRUE (seed_now);
			ASSERT_EQ (seed_now.value (), seed);
		}
		else
		{
			ASSERT_FALSE (true);
		}
	}
}

TEST (wallet, change_seed)
{
	nano::test::system system (1);
	auto wallet (system.wallet (0));
	nano::raw_key seed1;
	seed1 = 1;
	nano::public_key pub;
	uint32_t index (4);
	auto prv = nano::deterministic_key (seed1, index);
	pub = nano::pub_key (prv);
	wallet->insert_adhoc (nano::dev::genesis_key.prv, false);
	auto block (wallet->send_action (nano::dev::genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (5s, nano::test::exists (*system.nodes[0], { block }));
	{
		wallet->change_seed (seed1);
		auto seed2 = wallet->get_seed ();
		ASSERT_TRUE (seed2);
		ASSERT_EQ (seed1, seed2.value ());
		ASSERT_EQ (index + 1, wallet->get_deterministic_index ());
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, deterministic_restore)
{
	nano::test::system system (1);
	auto wallet (system.wallet (0));
	nano::raw_key seed1;
	seed1 = 1;
	nano::public_key pub;
	uint32_t index (4);
	{
		wallet->change_seed (seed1);
		auto seed2 = wallet->get_seed ();
		ASSERT_TRUE (seed2);
		ASSERT_EQ (seed1, seed2.value ());
		ASSERT_EQ (1, wallet->get_deterministic_index ());
		auto prv = nano::deterministic_key (seed1, index);
		pub = nano::pub_key (prv);
	}
	wallet->insert_adhoc (nano::dev::genesis_key.prv, false);
	auto block (wallet->send_action (nano::dev::genesis_key.pub, pub, 100));
	ASSERT_NE (nullptr, block);
	ASSERT_TIMELY (5s, nano::test::exists (*system.nodes[0], { block }));
	{
		wallet->deterministic_restore ();
		ASSERT_EQ (index + 1, wallet->get_deterministic_index ());
	}
	ASSERT_TRUE (wallet->exists (pub));
}

TEST (wallet, epoch_2_validation)
{
	nano::test::system system (1);
	auto & node (*system.nodes[0]);
	auto & wallet (*system.wallet (0));

	// Upgrade the genesis account to epoch 2
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, nano::epoch::epoch_1));
	ASSERT_NE (nullptr, system.upgrade_genesis_epoch (node, nano::epoch::epoch_2));

	wallet.insert_adhoc (nano::dev::genesis_key.prv, false);

	// Test send and receive blocks
	// An epoch 2 receive block should be generated with lower difficulty with high probability
	auto tries = 0;
	auto max_tries = 20;
	auto amount = node.config.receive_minimum.number ();
	while (++tries < max_tries)
	{
		auto send = wallet.send_action (nano::dev::genesis_key.pub, nano::dev::genesis_key.pub, amount, 1);
		ASSERT_NE (nullptr, send);
		ASSERT_EQ (nano::epoch::epoch_2, send->sideband ().details.epoch);
		ASSERT_EQ (nano::epoch::epoch_0, send->sideband ().source_epoch); // Not used for send state blocks

		auto receive = wallet.receive_action (send->hash (), nano::dev::genesis_key.pub, amount, send->destination (), 1);
		ASSERT_NE (nullptr, receive);
		if (nano::dev::network_params.work.difficulty (*receive) < node.network_params.work.base)
		{
			ASSERT_GE (nano::dev::network_params.work.difficulty (*receive), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (nano::epoch::epoch_2, receive->sideband ().details.epoch);
			ASSERT_EQ (nano::epoch::epoch_2, receive->sideband ().source_epoch);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);

	// Test a change block
	ASSERT_NE (nullptr, wallet.change_action (nano::dev::genesis_key.pub, nano::keypair ().pub, 1));
}

// Receiving from an upgraded account uses the lower threshold and upgrades the receiving account
TEST (wallet, epoch_2_receive_propagation)
{
	auto tries = 0;
	auto const max_tries = 20;
	while (++tries < max_tries)
	{
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, nano::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		nano::keypair key;
		nano::state_block_builder builder;

		// Send and open the account
		wallet.insert_adhoc (nano::dev::genesis_key.prv, false);
		wallet.insert_adhoc (key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (nano::dev::genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send1);
		ASSERT_NE (nullptr, wallet.receive_action (send1->hash (), nano::dev::genesis_key.pub, amount, send1->destination (), 1));

		// Upgrade the genesis account to epoch 2
		auto epoch2 = system.upgrade_genesis_epoch (node, nano::epoch::epoch_2);
		ASSERT_NE (nullptr, epoch2);

		// Send a block
		auto send2 = wallet.send_action (nano::dev::genesis_key.pub, key.pub, amount, 1);
		ASSERT_NE (nullptr, send2);

		auto receive2 = wallet.receive_action (send2->hash (), key.pub, amount, send2->destination (), 1);
		ASSERT_NE (nullptr, receive2);
		if (nano::dev::network_params.work.difficulty (*receive2) < node.network_params.work.base)
		{
			ASSERT_GE (nano::dev::network_params.work.difficulty (*receive2), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (nano::epoch::epoch_2, node.ledger.version (*receive2));
			ASSERT_EQ (nano::epoch::epoch_2, receive2->sideband ().source_epoch);
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
		nano::test::system system;
		nano::node_flags node_flags;
		node_flags.disable_request_loop = true;
		auto & node (*system.add_node (node_flags));
		auto & wallet (*system.wallet (0));

		// Upgrade the genesis account to epoch 1
		auto epoch1 = system.upgrade_genesis_epoch (node, nano::epoch::epoch_1);
		ASSERT_NE (nullptr, epoch1);

		nano::keypair key;
		nano::state_block_builder builder;

		// Send
		wallet.insert_adhoc (nano::dev::genesis_key.prv, false);
		auto amount = node.config.receive_minimum.number ();
		auto send1 = wallet.send_action (nano::dev::genesis_key.pub, key.pub, amount, 1);

		// Upgrade unopened account to epoch_2
		auto epoch2_unopened = builder
							   .account (key.pub)
							   .previous (0)
							   .representative (0)
							   .balance (0)
							   .link (node.network_params.ledger.epochs.link (nano::epoch::epoch_2))
							   .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
							   .work (*system.work.generate (key.pub, node.network_params.work.epoch_2))
							   .build ();
		ASSERT_EQ (nano::block_status::progress, node.process (epoch2_unopened));

		wallet.insert_adhoc (key.prv, false);

		auto receive1 = wallet.receive_action (send1->hash (), key.pub, amount, send1->destination (), 1);
		ASSERT_NE (nullptr, receive1);
		if (nano::dev::network_params.work.difficulty (*receive1) < node.network_params.work.base)
		{
			ASSERT_GE (nano::dev::network_params.work.difficulty (*receive1), node.network_params.work.epoch_2_receive);
			ASSERT_EQ (nano::epoch::epoch_2, node.ledger.version (*receive1));
			ASSERT_EQ (nano::epoch::epoch_1, receive1->sideband ().source_epoch);
			break;
		}
	}
	ASSERT_LT (tries, max_tries);
}

TEST (wallet, search_receivable)
{
	nano::test::system system;
	nano::node_config config = system.default_config ();
	config.enable_voting = false;
	config.backlog_scan->enable = false;
	nano::node_flags flags;
	flags.disable_search_pending = true;
	auto & node (*system.add_node (config, flags));
	auto & wallet (*system.wallet (0));

	wallet.insert_adhoc (nano::dev::genesis_key.prv);
	nano::block_builder builder;
	auto send = builder.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - node.config.receive_minimum.number ())
				.link (nano::dev::genesis_key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();
	ASSERT_EQ (nano::block_status::progress, node.process (send));

	// Pending search should start an election
	ASSERT_TRUE (node.active.empty ());
	ASSERT_FALSE (wallet.search_receivable ());
	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = node.active.election (send->qualified_root ()));

	// Erase the key so the confirmation does not trigger an automatic receive
	wallet.remove_account (nano::dev::genesis_key.pub);

	// Now confirm the election
	election->force_confirm ();

	ASSERT_TIMELY (5s, node.block_confirmed (send->hash ()) && node.active.empty ());

	// Re-insert the key
	wallet.insert_adhoc (nano::dev::genesis_key.prv);

	// Pending search should create the receive block
	ASSERT_EQ (2, node.ledger.block_count ());
	ASSERT_FALSE (wallet.search_receivable ());
	ASSERT_TIMELY_EQ (3s, node.balance (nano::dev::genesis_key.pub), nano::dev::constants.genesis_amount);
	auto receive_hash = node.ledger.any.account_head (node.ledger.tx_begin_read (), nano::dev::genesis_key.pub);
	auto receive = node.block (receive_hash);
	ASSERT_NE (nullptr, receive);
	ASSERT_EQ (receive->sideband ().height, 3);
	ASSERT_EQ (send->hash (), receive->source ());
}

TEST (wallet, receive_pruned)
{
	nano::test::system system;
	nano::node_flags node_flags;
	node_flags.disable_request_loop = true;
	auto & node1 = *system.add_node (node_flags);
	node_flags.enable_pruning = true;
	nano::node_config config = system.default_config ();
	config.enable_voting = false; // Remove after allowing pruned voting
	auto & node2 = *system.add_node (config, node_flags);

	auto & wallet1 = *system.wallet (0);
	auto & wallet2 = *system.wallet (1);

	nano::keypair key;
	nano::state_block_builder builder;

	// Send
	wallet1.insert_adhoc (nano::dev::genesis_key.prv, false);
	auto amount = node2.config.receive_minimum.number ();
	auto send1 = wallet1.send_action (nano::dev::genesis_key.pub, key.pub, amount, 1);
	auto send2 = wallet1.send_action (nano::dev::genesis_key.pub, key.pub, 1, 1);

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
