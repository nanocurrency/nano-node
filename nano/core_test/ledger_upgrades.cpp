#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/common.hpp>
#include <nano/store/backend.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/rep_weight.hpp>
#include <nano/store/ledger/version.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/tables.hpp>
#include <nano/store/typed_iterator.hpp>
#include <nano/test_common/common.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <filesystem>

using namespace std::chrono_literals;

namespace
{
/*
 * Schema definitions matching historical ledger versions
 */
nano::store::column_schema const schema_v21{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::unchecked, "unchecked" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const schema_v22{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::meta, "meta" }
};

nano::store::column_schema const schema_v23{
	{ nano::store::table::blocks, "blocks" },
	{ nano::store::table::accounts, "accounts" },
	{ nano::store::table::pending, "pending" },
	{ nano::store::table::rep_weights, "rep_weights" },
	{ nano::store::table::online_weight, "online_weight" },
	{ nano::store::table::pruned, "pruned" },
	{ nano::store::table::peers, "peers" },
	{ nano::store::table::confirmation_height, "confirmation_height" },
	{ nano::store::table::final_votes, "final_votes" },
	{ nano::store::table::frontiers, "frontiers" },
	{ nano::store::table::meta, "meta" }
};
}

/*
 * Test that opening a database with a version higher than supported fails
 */
TEST (ledger_upgrades, version_too_high)
{
	// Create database with a future version
	auto const path = nano::unique_path ();
	{
		auto backend = nano::test::make_backend (path);
		backend->create (nano::store::ledger_store::schema_current, 999);
	}

	// Attempting to open through ledger_store should throw
	ASSERT_THROW (
	nano::store::ledger_store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test that opening a database with a version lower than minimum fails
 */
TEST (ledger_upgrades, version_too_low)
{
	// Create database with a version below minimum
	auto const path = nano::unique_path ();
	{
		auto backend = nano::test::make_backend (path);
		backend->create (nano::store::ledger_store::schema_current, 7);
	}

	// Attempting to open through ledger_store should throw
	ASSERT_THROW (
	nano::store::ledger_store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test that read-only mode prevents upgrades
 */
TEST (ledger_upgrades, read_only_prevents_upgrade)
{
	// Create a v21 database
	auto const path = nano::unique_path ();
	{
		auto backend = nano::test::make_backend (path);
		backend->create (schema_v21, 21);
	}

	// Attempting to open in read-only mode should fail because upgrade is needed
	ASSERT_THROW (
	nano::store::ledger_store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_only,
	nano::test::default_stats (),
	nano::test::default_logger ()),
	std::runtime_error);
}

/*
 * Test opening an initialized store in read-only mode
 */
TEST (ledger_upgrades, current_version_read_only)
{
	// Create and initialize a current version database
	auto const path = nano::unique_path ();
	{
		nano::store::ledger_store store (
		nano::test::make_backend (path),
		nano::store::open_mode::read_write,
		nano::test::default_stats (),
		nano::test::default_logger ());

		auto tx = store.tx_begin_write ();
		store.initialize (tx, nano::dev::constants);
	}

	// Open in read-only mode - should succeed since no upgrade needed
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_only,
	nano::test::default_stats (),
	nano::test::default_logger ());

	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);

	// Verify we can read genesis account
	nano::account_info info;
	ASSERT_FALSE (store.account.get (tx, nano::dev::genesis_key.pub, info));
}

/*
 * Test that a current version database opens without upgrade
 */
TEST (ledger_upgrades, current_version_no_upgrade)
{
	// Create a current version database
	auto const path = nano::unique_path ();
	{
		nano::store::ledger_store store (
		nano::test::make_backend (path),
		nano::store::open_mode::read_write,
		nano::test::default_stats (),
		nano::test::default_logger ());

		auto tx = store.tx_begin_write ();
		store.initialize (tx, nano::dev::constants);
	}

	// Open again - should not require any upgrade
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);
}

namespace
{
class legacy_database_v21
{
public:
	legacy_database_v21 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ nano::test::make_backend (path_a) }
	{
		backend->create (schema_v21, 21);
		backend->open (schema_v21, nano::store::open_mode::read_write);
	}

	// Insert dummy data into unchecked table for testing upgrade that drops it
	void add_unchecked (uint64_t key_value, uint64_t data_value)
	{
		auto tx = backend->tx_begin_write ();
		nano::store::db_val key{ sizeof (key_value), &key_value };
		nano::store::db_val value{ sizeof (data_value), &data_value };
		auto status = backend->put (tx, nano::store::table::unchecked, key, value);
		backend->release_assert_success (status);
	}

	void add_account (nano::account const & account, nano::account_info_v22 const & info)
	{
		auto tx = backend->tx_begin_write ();
		auto status = backend->put (tx, nano::store::table::accounts, account, info);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<nano::store::backend> backend;
};
}

/*
 * Test v21 to v22 upgrade: removes unchecked table
 */
TEST (ledger_upgrades, upgrade_v21_to_v22)
{
	// Create a v21 database with data in unchecked table
	auto const path = nano::unique_path ();
	{
		legacy_database_v21 legacy_db{ path };
		legacy_db.add_unchecked (1, 100);
		legacy_db.add_unchecked (2, 200);

		// Verify unchecked table exists before upgrade
		ASSERT_TRUE (legacy_db.backend->table_exists ("unchecked"));
		auto tx = legacy_db.backend->tx_begin_read ();
		ASSERT_EQ (legacy_db.backend->count (tx, nano::store::table::unchecked), 2);
	}

	// Perform the upgrade
	{
		nano::store::ledger_store_params params;
		params.defer_open = true;

		nano::store::ledger_store store (
		nano::test::make_backend (path),
		nano::store::open_mode::read_write,
		nano::test::default_stats (),
		nano::test::default_logger (),
		params);

		store.upgrade_v21_to_v22 ();
	}

	// Verify version is now 22 and unchecked table no longer exists
	{
		auto backend = nano::test::make_backend (path);
		backend->open (schema_v22, nano::store::open_mode::read_only);
		auto tx = backend->tx_begin_read ();
		ASSERT_EQ (backend->get_version (tx), 22);
		ASSERT_FALSE (backend->table_exists ("unchecked"));
	}
}

namespace
{
class legacy_database_v22
{
public:
	legacy_database_v22 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ nano::test::make_backend (path_a) }
	{
		backend->create (schema_v22, 22);
		backend->open (schema_v22, nano::store::open_mode::read_write);
	}

	void add_account (nano::account const & account, nano::account_info_v22 const & info)
	{
		auto tx = backend->tx_begin_write ();
		auto status = backend->put (tx, nano::store::table::accounts, account, info);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<nano::store::backend> backend;
};
}

/*
 * Test v22 to v23 upgrade: populates rep_weights from account data
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_rep_weights)
{
	nano::account const account_1{ 1 };
	nano::account const account_2{ 2 };
	nano::account const account_3{ 3 };
	nano::account const rep_a{ 41 };
	nano::account const rep_b{ 43 };

	// Create a v22 database with test accounts
	auto const path = nano::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		// Account 1: balance 1000, rep_a
		nano::account_info_v22 info1{};
		info1.representative = rep_a;
		info1.balance = 1000;
		legacy_db.add_account (account_1, info1);

		// Account 2: balance 500, rep_a (same rep as account 1)
		nano::account_info_v22 info2{};
		info2.representative = rep_a;
		info2.balance = 500;
		legacy_db.add_account (account_2, info2);

		// Account 3: balance 42, rep_b
		nano::account_info_v22 info3{};
		info3.representative = rep_b;
		info3.balance = 42;
		legacy_db.add_account (account_3, info3);
	}

	// Open through ledger_store which should trigger upgrade
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	// Verify rep weights were correctly calculated
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);

	// rep_a should have weight from account_1 + account_2 = 1000 + 500 = 1500
	ASSERT_EQ (store.rep_weight.get (tx, rep_a), 1500);

	// rep_b should have weight from account_3 = 42
	ASSERT_EQ (store.rep_weight.get (tx, rep_b), 42);
}

/*
 * Test v22 to v23 upgrade: zero balance accounts don't contribute to rep weight
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_zero_balance)
{
	nano::account const rep{ 100 };
	nano::account const account_with_balance{ 1 };
	nano::account const account_zero_balance{ 2 };

	// Create a v22 database with accounts (one with zero balance)
	auto const path = nano::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		nano::account_info_v22 info1{};
		info1.representative = rep;
		info1.balance = 1000;
		legacy_db.add_account (account_with_balance, info1);

		nano::account_info_v22 info2{};
		info2.representative = rep;
		info2.balance = 0; // Zero balance
		legacy_db.add_account (account_zero_balance, info2);
	}

	// Open through ledger_store which should trigger upgrade
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	// Verify rep weight only includes non-zero balance accounts
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.rep_weight.get (tx, rep), 1000);
}

/*
 * Test v22 to v23 upgrade with many accounts to verify batch processing
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_batch_processing)
{
	// Create multiple reps with multiple accounts
	std::vector<nano::account> reps;
	std::map<nano::account, nano::uint128_t> expected_weights;

	for (int i = 0; i < 5; ++i)
	{
		reps.push_back (nano::account{ static_cast<uint64_t> (1000 + i) });
		expected_weights[reps.back ()] = 0;
	}

	// Create a v22 database with many accounts
	auto const path = nano::unique_path ();
	{
		legacy_database_v22 legacy_db{ path };

		for (int i = 0; i < 50; ++i)
		{
			nano::account account{ static_cast<uint64_t> (i + 1) };
			auto & rep = reps[i % reps.size ()];
			nano::uint128_t balance = (i + 1) * 100;

			nano::account_info_v22 info{};
			info.representative = rep;
			info.balance = balance;
			legacy_db.add_account (account, info);

			expected_weights[rep] += balance;
		}
	}

	// Open through ledger_store which should trigger upgrade
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	// Verify rep weights were correctly calculated
	auto tx = store.tx_begin_read ();
	for (auto const & [rep, expected_weight] : expected_weights)
	{
		ASSERT_EQ (store.rep_weight.get (tx, rep), expected_weight)
		<< "Rep weight mismatch for rep " << rep.to_account ();
	}
}

/*
 * Test v22 to v23 upgrade: handles partially populated rep_weights from failed upgrade
 * This simulates a scenario where a previous upgrade attempt wrote some data to rep_weights but didn't complete.
 */
TEST (ledger_upgrades, upgrade_v22_to_v23_stale_rep_weights)
{
	nano::account const rep_a{ 41 };
	nano::account const rep_b{ 43 };
	nano::account const rep_stale{ 99 }; // Stale entry from previous failed upgrade
	nano::account const account_1{ 1 };
	nano::account const account_2{ 2 };

	auto const path = nano::unique_path ();
	{
		// Create v22 database with accounts
		legacy_database_v22 legacy_db{ path };

		nano::account_info_v22 info1{};
		info1.representative = rep_a;
		info1.balance = 1000;
		legacy_db.add_account (account_1, info1);

		nano::account_info_v22 info2{};
		info2.representative = rep_b;
		info2.balance = 500;
		legacy_db.add_account (account_2, info2);
	}

	// Simulate a failed upgrade that partially populated rep_weights
	{
		auto backend = nano::test::make_backend (path);
		// Open with schema_v23 to create rep_weights table (but version stays at 22)
		backend->open (schema_v23, nano::store::open_mode::read_write);
		{
			auto tx = backend->tx_begin_write ();

			// Add stale/incorrect data as if upgrade crashed mid-way
			// - rep_a has wrong value
			// - rep_stale shouldn't exist (was from deleted account in previous attempt)
			backend->put (tx, nano::store::table::rep_weights, rep_a, nano::amount{ 999999 }); // Wrong value
			backend->put (tx, nano::store::table::rep_weights, rep_stale, nano::amount{ 12345 }); // Stale entry

			// Version is still 22 (upgrade didn't complete)
			ASSERT_EQ (backend->get_version (tx), 22);
		}
		backend->close ();
	}

	// Now open through ledger_store which should properly handle the crash recovery
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	// Verify rep weights are correct (not corrupted by stale data)
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);

	// rep_a should have correct weight from account_1 = 1000 (not stale 999999)
	ASSERT_EQ (store.rep_weight.get (tx, rep_a), 1000);

	// rep_b should have correct weight from account_2 = 500
	ASSERT_EQ (store.rep_weight.get (tx, rep_b), 500);

	// rep_stale should not exist (it was cleared during proper upgrade)
	ASSERT_EQ (store.rep_weight.get (tx, rep_stale), 0);
}

namespace
{
class legacy_database_v23
{
public:
	legacy_database_v23 (std::filesystem::path const & path_a) :
		path{ path_a },
		backend{ nano::test::make_backend (path_a) }
	{
		backend->create (schema_v23, 23);
		backend->open (schema_v23, nano::store::open_mode::read_write);
	}

	// Insert dummy data into frontiers table for testing upgrade that drops it
	void add_frontier (uint64_t key_value, uint64_t data_value)
	{
		auto tx = backend->tx_begin_write ();
		nano::store::db_val key{ sizeof (key_value), &key_value };
		nano::store::db_val value{ sizeof (data_value), &data_value };
		auto status = backend->put (tx, nano::store::table::frontiers, key, value);
		backend->release_assert_success (status);
	}

	std::filesystem::path path;
	std::unique_ptr<nano::store::backend> backend;
};
}

/*
 * Test v23 to v24 upgrade: removes frontiers table
 */
TEST (ledger_upgrades, upgrade_v23_to_v24)
{
	// Create a v23 database with data in frontiers table
	auto const path = nano::unique_path ();
	{
		legacy_database_v23 legacy_db{ path };
		legacy_db.add_frontier (1, 100);
		legacy_db.add_frontier (2, 200);

		// Verify frontiers table exists before upgrade
		ASSERT_TRUE (legacy_db.backend->table_exists ("frontiers"));
		auto tx = legacy_db.backend->tx_begin_read ();
		ASSERT_EQ (legacy_db.backend->count (tx, nano::store::table::frontiers), 2);
	}

	// Perform the upgrade
	{
		nano::store::ledger_store_params params;
		params.defer_open = true;

		nano::store::ledger_store store (
		nano::test::make_backend (path),
		nano::store::open_mode::read_write,
		nano::test::default_stats (),
		nano::test::default_logger (),
		params);

		store.upgrade_v23_to_v24 ();
	}

	// Verify version is now 24 and frontiers table no longer exists
	{
		auto backend = nano::test::make_backend (path);
		backend->open (nano::store::ledger_store::schema_current, nano::store::open_mode::read_only);
		auto tx = backend->tx_begin_read ();
		ASSERT_EQ (backend->get_version (tx), 24);
		ASSERT_FALSE (backend->table_exists ("frontiers"));
	}
}

/*
 * Test full upgrade path from v21 to current version
 */
TEST (ledger_upgrades, full_upgrade_v21_to_current)
{
	nano::account const rep{ 100 };
	nano::account const account{ 1 };

	// Create a v21 database with test data
	auto const path = nano::unique_path ();
	{
		legacy_database_v21 legacy_db{ path };

		nano::account_info_v22 info{};
		info.representative = rep;
		info.balance = 5000;
		legacy_db.add_account (account, info);
	}

	// Open through ledger_store which should trigger full upgrade chain
	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger ());

	// Verify final version
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);

	// Verify account still exists
	nano::account_info account_info;
	ASSERT_FALSE (store.account.get (tx, account, account_info));
	ASSERT_EQ (account_info.balance, 5000);

	// Verify rep_weight was populated during v22->v23 upgrade
	ASSERT_EQ (store.rep_weight.get (tx, rep), 5000);
}

/*
 * Test that backup is created before upgrade
 */
TEST (ledger_upgrades, upgrade_backup)
{
	auto const path = nano::unique_path ();
	auto const is_rocksdb = nano::default_database_backend () == nano::database_backend::rocksdb;

	// Helper to check if backup exists
	auto backup_exists = [&] () {
		if (is_rocksdb)
		{
			// RocksDB creates backup/ directory
			return std::filesystem::exists (path / "backup");
		}
		else
		{
			// LMDB creates data_backup_<timestamp>.ldb file
			for (auto const & entry : std::filesystem::directory_iterator (path))
			{
				if (entry.path ().filename ().string ().find ("data_backup_") != std::string::npos)
				{
					return true;
				}
			}
			return false;
		}
	};

	// Create a v21 database (requires upgrade to current)
	{
		auto backend = nano::test::make_backend (path);
		backend->create (schema_v21, 21);
	}

	// Verify no backup exists yet
	ASSERT_FALSE (backup_exists ());

	// Open with backup_before_upgrade=true, triggering upgrade and backup
	nano::store::ledger_store_params params;
	params.backup_before_upgrade = true;

	nano::store::ledger_store store (
	nano::test::make_backend (path),
	nano::store::open_mode::read_write,
	nano::test::default_stats (),
	nano::test::default_logger (),
	params);

	// Verify version was upgraded
	auto tx = store.tx_begin_read ();
	ASSERT_EQ (store.version.get (tx), nano::store::ledger_store::version_current);

	// Verify backup was created
	ASSERT_TRUE (backup_exists ());
}
