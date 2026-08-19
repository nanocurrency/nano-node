#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/store/db_val_templ.hpp>
#include <nano/store/iterator.hpp>
#include <nano/store/transaction.hpp>
#include <nano/wallet/lmdb/wallets_backend_lmdb.hpp>
#include <nano/wallet/wallet_value.hpp>
#include <nano/wallet/wallets_backend.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace nano;

namespace
{
std::unique_ptr<nano::wallet::wallets_backend> make_wallets_backend (std::filesystem::path const & path)
{
	return std::make_unique<nano::wallet::lmdb::wallets_backend_lmdb> (path);
}

std::unique_ptr<nano::wallet::wallets_backend> make_wallets_backend ()
{
	return make_wallets_backend (nano::unique_path () / "wallet.ldb");
}

// Convenience: collect all (account, wallet_value) pairs from an iterator range
auto collect_accounts (nano::store::iterator begin, nano::store::iterator const & end) -> std::vector<std::pair<nano::account, nano::wallet::wallet_value>>
{
	std::vector<std::pair<nano::account, nano::wallet::wallet_value>> result;
	for (auto it = std::move (begin); it != end; ++it)
	{
		result.emplace_back (static_cast<nano::account> (nano::store::db_val{ it->first }), nano::wallet::wallet_value{ nano::store::db_val{ it->second } });
	}
	return result;
}

// Convenience: collect raw keys from an iterator range as strings
auto collect_keys (nano::store::iterator begin, nano::store::iterator const & end) -> std::vector<std::string>
{
	std::vector<std::string> result;
	for (auto it = std::move (begin); it != end; ++it)
	{
		result.push_back (nano::bytes_to_string (it->first));
	}
	return result;
}

nano::wallet::wallet_value make_value (uint64_t seed)
{
	nano::raw_key key;
	key = seed;
	return nano::wallet::wallet_value{ key, seed };
}
}

/*
 *
 */

TEST (wallets_backend, tx_begin_read_write)
{
	auto backend = make_wallets_backend ();
	auto wallet_read_txn = backend->tx_begin_read ();
	auto wallet_write_txn = backend->tx_begin_write ();
	// Just constructing/destroying a read txn must not throw.
}

TEST (wallets_backend, tx_single_writer)
{
	auto backend = make_wallets_backend ();
	std::atomic<bool> second_txn_begun{ false };
	std::thread second_writer;
	{
		auto first_wallet_txn = backend->tx_begin_write ();
		second_writer = std::thread ([&] {
			auto second_wallet_txn = backend->tx_begin_write ();
			second_txn_begun.store (true);
		});
		// With the first write txn live, the second must block. 100ms is generous
		// enough that flake would indicate a real contract violation, not noise.
		std::this_thread::sleep_for (std::chrono::milliseconds (100));
		ASSERT_FALSE (second_txn_begun.load ());
	}
	// first_wallet_txn destroyed -> write lock released -> second writer unblocks.
	second_writer.join ();
	ASSERT_TRUE (second_txn_begun.load ());
}

/*
 * Wallet sub-table lifecycle
 */

TEST (wallets_backend, wallet_open_or_create_creates_and_is_idempotent)
{
	auto backend = make_wallets_backend ();
	auto id = nano::random_wallet_id ().to_string ();
	auto wallet_txn = backend->tx_begin_write ();

	auto handle1 = backend->wallet_open_or_create (wallet_txn, id);
	ASSERT_TRUE (handle1.valid ());

	// Reopening the same wallet returns an equivalent handle (LMDB caches DBIs).
	auto handle2 = backend->wallet_open_or_create (wallet_txn, id);
	ASSERT_TRUE (handle2.valid ());
	ASSERT_EQ (handle1, handle2);
}

TEST (wallets_backend, wallet_open_or_create_distinct_ids_distinct_handles)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();

	auto handle_a = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());
	auto handle_b = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	ASSERT_TRUE (handle_a.valid ());
	ASSERT_TRUE (handle_b.valid ());
	ASSERT_NE (handle_a, handle_b);
}

TEST (wallets_backend, wallet_drop_clears_handle_and_data)
{
	auto backend = make_wallets_backend ();
	auto id = nano::random_wallet_id ().to_string ();
	{
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);
		backend->entry_put (wallet_txn, handle, nano::account{ 7 }, make_value (42));
		ASSERT_TRUE (backend->entry_exists (wallet_txn, handle, nano::account{ 7 }));

		backend->wallet_drop (wallet_txn, handle);
		// wallet_drop zeroes the handle so callers can't reuse it.
		ASSERT_FALSE (handle.valid ());
	}
	{
		// Reopening the same wallet id after a drop yields a fresh empty sub-table.
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);
		ASSERT_TRUE (handle.valid ());
		ASSERT_FALSE (backend->entry_exists (wallet_txn, handle, nano::account{ 7 }));
	}
}

TEST (wallets_backend, wallet_drop_empty_handle)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	nano::wallet::wallet_handle handle;
	ASSERT_FALSE (handle.valid ());
	backend->wallet_drop (wallet_txn, handle);
	ASSERT_FALSE (handle.valid ());
}

/*
 * Entry KV operations
 */

TEST (wallets_backend, entry_get_missing_returns_nullopt)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());
	auto value = backend->entry_get (wallet_txn, handle, nano::account{ 1 });
	ASSERT_FALSE (value.has_value ());
}

TEST (wallets_backend, entry_put_get_roundtrip)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	auto original = make_value (123);
	backend->entry_put (wallet_txn, handle, nano::account{ 9 }, original);

	auto retrieved = backend->entry_get (wallet_txn, handle, nano::account{ 9 });
	ASSERT_TRUE (retrieved.has_value ());
	auto retrieved_value = nano::wallet::wallet_value{ *retrieved };
	ASSERT_EQ (retrieved_value.key, original.key);
	ASSERT_EQ (retrieved_value.work, original.work);
}

TEST (wallets_backend, entry_put_overwrites)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	backend->entry_put (wallet_txn, handle, nano::account{ 1 }, make_value (10));
	backend->entry_put (wallet_txn, handle, nano::account{ 1 }, make_value (20));

	auto retrieved = backend->entry_get (wallet_txn, handle, nano::account{ 1 });
	ASSERT_TRUE (retrieved.has_value ());
	ASSERT_EQ (nano::wallet::wallet_value{ *retrieved }.work, 20);
}

TEST (wallets_backend, entry_del_removes)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	backend->entry_put (wallet_txn, handle, nano::account{ 5 }, make_value (1));
	ASSERT_TRUE (backend->entry_exists (wallet_txn, handle, nano::account{ 5 }));

	backend->entry_del (wallet_txn, handle, nano::account{ 5 });
	ASSERT_FALSE (backend->entry_exists (wallet_txn, handle, nano::account{ 5 }));
	ASSERT_FALSE (backend->entry_get (wallet_txn, handle, nano::account{ 5 }).has_value ());
}

TEST (wallets_backend, entry_exists_distinguishes_present_and_missing)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	ASSERT_FALSE (backend->entry_exists (wallet_txn, handle, nano::account{ 99 }));
	backend->entry_put (wallet_txn, handle, nano::account{ 99 }, make_value (1));
	ASSERT_TRUE (backend->entry_exists (wallet_txn, handle, nano::account{ 99 }));
}

TEST (wallets_backend, entries_isolated_per_wallet)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();

	auto handle_a = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());
	auto handle_b = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	backend->entry_put (wallet_txn, handle_a, nano::account{ 1 }, make_value (100));
	backend->entry_put (wallet_txn, handle_b, nano::account{ 1 }, make_value (200));

	auto from_a = backend->entry_get (wallet_txn, handle_a, nano::account{ 1 });
	auto from_b = backend->entry_get (wallet_txn, handle_b, nano::account{ 1 });
	ASSERT_TRUE (from_a.has_value ());
	ASSERT_TRUE (from_b.has_value ());
	ASSERT_EQ (nano::wallet::wallet_value{ *from_a }.work, 100);
	ASSERT_EQ (nano::wallet::wallet_value{ *from_b }.work, 200);
}

TEST (wallets_backend, entries_visible_in_read_txn_after_write_commit)
{
	auto backend = make_wallets_backend ();
	auto id = nano::random_wallet_id ().to_string ();
	{
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);
		backend->entry_put (wallet_txn, handle, nano::account{ 11 }, make_value (1));
	}
	auto wallet_txn = backend->tx_begin_read ();
	auto handle = backend->wallet_open_or_create (backend->tx_begin_write (), id);
	auto value = backend->entry_get (wallet_txn, handle, nano::account{ 11 });
	ASSERT_TRUE (value.has_value ());
	ASSERT_EQ (nano::wallet::wallet_value{ *value }.work, 1);
}

/*
 * Entries iteration
 */

TEST (wallets_backend, entries_iterate_empty)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	auto entries = collect_accounts (backend->entries_begin (wallet_txn, handle), backend->entries_end (wallet_txn, handle));
	ASSERT_TRUE (entries.empty ());
}

TEST (wallets_backend, entries_iterate_sorted)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	// Insert in scrambled order.
	std::vector<uint64_t> const seeds{ 7, 2, 99, 1 };
	for (auto seed : seeds)
	{
		backend->entry_put (wallet_txn, handle, nano::account{ seed }, make_value (seed));
	}
	std::vector<nano::account> inserted;
	inserted.reserve (seeds.size ());
	for (auto seed : seeds)
	{
		inserted.emplace_back (seed);
	}

	auto entries = collect_accounts (backend->entries_begin (wallet_txn, handle), backend->entries_end (wallet_txn, handle));
	ASSERT_EQ (entries.size (), inserted.size ());

	// Result must be sorted by account.
	for (size_t i = 1; i < entries.size (); ++i)
	{
		ASSERT_LT (entries[i - 1].first, entries[i].first);
	}

	// And contain exactly the inserted set.
	std::set<nano::account> got;
	for (auto const & e : entries)
	{
		got.insert (e.first);
	}
	std::set<nano::account> expected{ inserted.begin (), inserted.end () };
	ASSERT_EQ (got, expected);
}

TEST (wallets_backend, entries_iterate_lower_bound)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	for (uint64_t i : { 1, 5, 10, 20, 30 })
	{
		backend->entry_put (wallet_txn, handle, nano::account{ i }, make_value (i));
	}

	// Lower bound at 10 must skip 1 and 5.
	std::vector<nano::account> seen;
	for (auto it = backend->entries_begin (wallet_txn, handle, nano::store::db_val{ nano::account{ 10 } }),
			  end = backend->entries_end (wallet_txn, handle);
		 it != end;
		 ++it)
	{
		seen.push_back (static_cast<nano::account> (nano::store::db_val{ it->first }));
	}
	ASSERT_EQ (seen, (std::vector<nano::account>{ nano::account{ 10 }, nano::account{ 20 }, nano::account{ 30 } }));

	// Lower bound at 11 must skip 10 too.
	seen.clear ();
	for (auto it = backend->entries_begin (wallet_txn, handle, nano::store::db_val{ nano::account{ 11 } }),
			  end = backend->entries_end (wallet_txn, handle);
		 it != end;
		 ++it)
	{
		seen.push_back (static_cast<nano::account> (nano::store::db_val{ it->first }));
	}
	ASSERT_EQ (seen, (std::vector<nano::account>{ nano::account{ 20 }, nano::account{ 30 } }));
}

TEST (wallets_backend, entries_iterate_lower_bound_past_end)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	auto handle = backend->wallet_open_or_create (wallet_txn, nano::random_wallet_id ().to_string ());

	backend->entry_put (wallet_txn, handle, nano::account{ 1 }, make_value (1));
	backend->entry_put (wallet_txn, handle, nano::account{ 2 }, make_value (2));

	auto begin = backend->entries_begin (wallet_txn, handle, nano::store::db_val{ nano::account{ 100 } });
	auto end = backend->entries_end (wallet_txn, handle);
	ASSERT_EQ (begin, end);
}

/*
 * Wallet-index iteration
 */

TEST (wallets_backend, index_iterate_lists_created_wallets)
{
	auto backend = make_wallets_backend ();

	std::set<std::string> created;
	{
		auto wallet_txn = backend->tx_begin_write ();
		for (int i = 0; i < 3; ++i)
		{
			auto id = nano::random_wallet_id ().to_string ();
			created.insert (id);
			backend->wallet_open_or_create (wallet_txn, id);
		}
	}

	auto wallet_txn = backend->tx_begin_read ();
	auto keys = collect_keys (backend->index_begin (wallet_txn), backend->index_end (wallet_txn));

	// Every created id must appear in the index. Other entries (e.g. send_action_ids on LMDB)
	// may also be present — the contract is "may contain non-wallet entries".
	std::set<std::string> seen{ keys.begin (), keys.end () };
	for (auto const & id : created)
	{
		ASSERT_TRUE (seen.contains (id)) << "missing wallet id " << id;
	}
}

TEST (wallets_backend, index_iterate_after_drop)
{
	auto backend = make_wallets_backend ();
	auto kept = nano::random_wallet_id ().to_string ();
	auto dropped = nano::random_wallet_id ().to_string ();

	{
		auto wallet_txn = backend->tx_begin_write ();
		backend->wallet_open_or_create (wallet_txn, kept);
		auto handle = backend->wallet_open_or_create (wallet_txn, dropped);
		backend->wallet_drop (wallet_txn, handle);
	}

	auto wallet_txn = backend->tx_begin_read ();
	auto keys = collect_keys (backend->index_begin (wallet_txn), backend->index_end (wallet_txn));
	std::set<std::string> seen{ keys.begin (), keys.end () };
	ASSERT_TRUE (seen.contains (kept));
	ASSERT_FALSE (seen.contains (dropped));
}

TEST (wallets_backend, index_iterate_lower_bound)
{
	auto backend = make_wallets_backend ();

	// Sub-DB names sort lexicographically as ASCII; pick deterministic hex strings.
	std::string const id_low = std::string (64, '0');
	std::string const id_mid (64, '7');
	std::string const id_high (64, 'F');
	{
		auto wallet_txn = backend->tx_begin_write ();
		backend->wallet_open_or_create (wallet_txn, id_low);
		backend->wallet_open_or_create (wallet_txn, id_mid);
		backend->wallet_open_or_create (wallet_txn, id_high);
	}

	auto wallet_txn = backend->tx_begin_read ();
	auto seen = collect_keys (backend->index_begin (wallet_txn, nano::store::db_val{ id_mid }), backend->index_end (wallet_txn));

	std::set<std::string> seen_set{ seen.begin (), seen.end () };
	ASSERT_FALSE (seen_set.contains (id_low));
	ASSERT_TRUE (seen_set.contains (id_mid));
	ASSERT_TRUE (seen_set.contains (id_high));
}

/*
 * Send action IDs
 */

TEST (wallets_backend, send_action_id_get_missing_returns_nullopt)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_read ();
	auto value = backend->send_action_id_get (wallet_txn, "nonexistent");
	ASSERT_FALSE (value.has_value ());
}

TEST (wallets_backend, send_action_id_put_get_roundtrip)
{
	auto backend = make_wallets_backend ();
	nano::block_hash hash{ 0xdeadbeef };
	{
		auto wallet_txn = backend->tx_begin_write ();
		ASSERT_TRUE (backend->send_action_id_put (wallet_txn, "id-a", hash));
	}
	auto wallet_txn = backend->tx_begin_read ();
	auto retrieved = backend->send_action_id_get (wallet_txn, "id-a");
	ASSERT_TRUE (retrieved.has_value ());
	ASSERT_EQ (static_cast<nano::block_hash> (*retrieved), hash);
}

TEST (wallets_backend, send_action_id_put_overwrites)
{
	auto backend = make_wallets_backend ();
	auto wallet_txn = backend->tx_begin_write ();
	nano::block_hash first{ 1 };
	nano::block_hash second{ 2 };
	ASSERT_TRUE (backend->send_action_id_put (wallet_txn, "id-a", first));
	ASSERT_TRUE (backend->send_action_id_put (wallet_txn, "id-a", second));
	auto retrieved = backend->send_action_id_get (wallet_txn, "id-a");
	ASSERT_TRUE (retrieved.has_value ());
	ASSERT_EQ (static_cast<nano::block_hash> (*retrieved), second);
}

TEST (wallets_backend, send_action_ids_clear_empties_table)
{
	auto backend = make_wallets_backend ();
	{
		auto wallet_txn = backend->tx_begin_write ();
		backend->send_action_id_put (wallet_txn, "id-a", nano::block_hash{ 1 });
		backend->send_action_id_put (wallet_txn, "id-b", nano::block_hash{ 2 });
	}
	{
		auto wallet_txn = backend->tx_begin_write ();
		backend->send_action_ids_clear (wallet_txn);
	}
	auto wallet_txn = backend->tx_begin_read ();
	ASSERT_FALSE (backend->send_action_id_get (wallet_txn, "id-a").has_value ());
	ASSERT_FALSE (backend->send_action_id_get (wallet_txn, "id-b").has_value ());
}

TEST (wallets_backend, send_action_ids_clear_preserves_table_for_future_puts)
{
	auto backend = make_wallets_backend ();
	{
		auto wallet_txn = backend->tx_begin_write ();
		backend->send_action_id_put (wallet_txn, "id-a", nano::block_hash{ 1 });
		backend->send_action_ids_clear (wallet_txn);
		ASSERT_TRUE (backend->send_action_id_put (wallet_txn, "id-b", nano::block_hash{ 2 }));
	}
	auto wallet_txn = backend->tx_begin_read ();
	auto retrieved = backend->send_action_id_get (wallet_txn, "id-b");
	ASSERT_TRUE (retrieved.has_value ());
	ASSERT_EQ (static_cast<nano::block_hash> (*retrieved), nano::block_hash{ 2 });
}

/*
 * Persistence across backend instances
 */

TEST (wallets_backend, data_persists_across_backend_reopen)
{
	auto path = nano::unique_path () / "wallet.ldb";
	auto id = nano::random_wallet_id ().to_string ();

	{
		auto backend = make_wallets_backend (path);
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);
		backend->entry_put (wallet_txn, handle, nano::account{ 42 }, make_value (777));
		backend->send_action_id_put (wallet_txn, "id-x", nano::block_hash{ 0xfeedface });
	}
	{
		auto backend = make_wallets_backend (path);
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);

		auto entry = backend->entry_get (wallet_txn, handle, nano::account{ 42 });
		ASSERT_TRUE (entry.has_value ());
		ASSERT_EQ (nano::wallet::wallet_value{ *entry }.work, 777);

		auto hash = backend->send_action_id_get (wallet_txn, "id-x");
		ASSERT_TRUE (hash.has_value ());
		ASSERT_EQ (static_cast<nano::block_hash> (*hash), nano::block_hash{ 0xfeedface });
	}
}

/*
 * Environment
 */

TEST (wallets_backend, database_path_returns_constructed_path)
{
	auto path = nano::unique_path () / "wallet.ldb";
	auto backend = make_wallets_backend (path);
	ASSERT_EQ (backend->database_path (), path);
}

namespace
{
// Backups are written next to the database as `wallet_backup_<timestamp>.ldb`
std::vector<std::filesystem::path> backup_files (std::filesystem::path const & dir)
{
	std::vector<std::filesystem::path> result;
	for (auto const & entry : std::filesystem::directory_iterator{ dir })
	{
		if (entry.path ().filename ().string ().starts_with ("wallet_backup_"))
		{
			result.push_back (entry.path ());
		}
	}
	return result;
}
}

TEST (wallets_backend, backup_creates_file_alongside_database)
{
	auto path = nano::unique_path () / "wallet.ldb";
	auto backend = make_wallets_backend (path);
	ASSERT_TRUE (backup_files (path.parent_path ()).empty ());

	nano::logger logger;
	backend->backup (logger);

	ASSERT_EQ (backup_files (path.parent_path ()).size (), 1);
}

TEST (wallets_backend, backup_is_openable_copy_of_database)
{
	auto path = nano::unique_path () / "wallet.ldb";
	auto id = nano::random_wallet_id ().to_string ();
	auto backend = make_wallets_backend (path);
	{
		auto wallet_txn = backend->tx_begin_write ();
		auto handle = backend->wallet_open_or_create (wallet_txn, id);
		backend->entry_put (wallet_txn, handle, nano::account{ 42 }, make_value (777));
	}

	nano::logger logger;
	backend->backup (logger);

	auto backups = backup_files (path.parent_path ());
	ASSERT_EQ (backups.size (), 1);

	// The backup must be a self-contained database holding the same data.
	auto restored = make_wallets_backend (backups.front ());
	auto wallet_txn = restored->tx_begin_write ();
	auto handle = restored->wallet_open_or_create (wallet_txn, id);
	auto entry = restored->entry_get (wallet_txn, handle, nano::account{ 42 });
	ASSERT_TRUE (entry.has_value ());
	ASSERT_EQ (nano::wallet::wallet_value{ *entry }.work, 777);
}
