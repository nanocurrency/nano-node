#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/migrations.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/confirmation_height.hpp>
#include <nano/store/ledger/final_vote.hpp>
#include <nano/store/ledger/online_weight.hpp>
#include <nano/store/ledger/peer.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger/pruned.hpp>
#include <nano/store/ledger/rep_weight.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/store/rocksdb/backend_rocksdb.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
struct migration_test_data
{
	std::vector<std::shared_ptr<nano::block>> blocks;
	std::map<nano::account, nano::account_info> accounts;
	std::map<nano::pending_key, nano::pending_info> pending;
	std::map<nano::account, nano::uint128_t> rep_weights;
	std::map<uint64_t, nano::amount> online_weights;
	std::set<nano::block_hash> pruned;
	std::vector<std::pair<nano::endpoint_key, uint64_t>> peers;
	std::map<nano::account, nano::confirmation_height_info> confirmation_heights;
	std::map<nano::qualified_root, nano::block_hash> final_votes;
};

constexpr size_t num_blocks = 100;
constexpr size_t num_accounts = 100;
constexpr size_t num_pending = 100;
constexpr size_t num_rep_weights = 50;
constexpr size_t num_online_weights = 20;
constexpr size_t num_pruned = 1000;
constexpr size_t num_peers = 50;
constexpr size_t num_confirmation_heights = 100;
constexpr size_t num_final_votes = 1000;

migration_test_data populate_ledger_for_migration (nano::store::ledger_store & store)
{
	migration_test_data data;

	nano::work_pool pool{ nano::dev::network_params.network, std::numeric_limits<unsigned>::max () };

	// Generate blocks
	auto previous = nano::dev::genesis->hash ();
	for (size_t i = 0; i < num_blocks; ++i)
	{
		auto block = nano::state_block_builder ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (previous)
					 .representative (nano::dev::genesis_key.pub)
					 .balance (nano::dev::constants.genesis_amount - (i + 1) * 1000)
					 .link (nano::account (i + 1))
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (previous))
					 .build ();
		data.blocks.push_back (block);
		previous = block->hash ();
	}

	// Populate accounts
	for (size_t i = 0; i < num_accounts; ++i)
	{
		nano::account acc{ i + 1 };
		nano::account_info info{ data.blocks[0]->hash (), nano::dev::genesis_key.pub,
			data.blocks[0]->hash (), 1000, nano::seconds_since_epoch (),
			1, nano::epoch::epoch_0 };
		data.accounts[acc] = info;
	}

	// Populate pending
	for (size_t i = 0; i < num_pending; ++i)
	{
		nano::pending_key key{ nano::account (i + 1), data.blocks[i]->hash () };
		nano::pending_info info{ nano::dev::genesis_key.pub, 1000, nano::epoch::epoch_0 };
		data.pending[key] = info;
	}

	// Populate rep_weights
	for (size_t i = 0; i < num_rep_weights; ++i)
	{
		nano::account rep{ i + 1 };
		data.rep_weights[rep] = (i + 1) * 1000000;
	}

	// Populate online_weight
	for (size_t i = 0; i < num_online_weights; ++i)
	{
		data.online_weights[i * 1000 + 100] = nano::amount (i * 1000000);
	}

	// Populate pruned
	for (size_t i = 0; i < num_pruned; ++i)
	{
		data.pruned.insert (nano::block_hash{ i + 10000 });
	}

	// Populate peers
	for (size_t i = 0; i < num_peers; ++i)
	{
		auto addr = boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1");
		nano::endpoint_key key (addr.to_bytes (), static_cast<uint16_t> (i + 1000));
		data.peers.emplace_back (key, 30);
	}

	// Populate confirmation_height
	for (size_t i = 0; i < num_confirmation_heights; ++i)
	{
		nano::account acc{ i + 1 };
		data.confirmation_heights[acc] = { i + 1, data.blocks[i % data.blocks.size ()]->hash () };
	}

	// Populate final_votes
	for (size_t i = 0; i < num_final_votes; ++i)
	{
		nano::qualified_root qroot{ nano::root{ i + 1 }, nano::block_hash{ i + 5000 } };
		data.final_votes[qroot] = nano::block_hash{ i + 10000 };
	}

	// Write all data to store
	{
		auto tx = store.tx_begin_write ();

		for (size_t i = 0; i < data.blocks.size (); ++i)
		{
			data.blocks[i]->sideband_set (nano::block_sideband{
			nano::dev::genesis_key.pub,
			nano::dev::constants.genesis_amount - (i + 1) * 1000,
			i + 2, nano::seconds_since_epoch (), nano::epoch::epoch_0,
			false, false, false, nano::epoch::epoch_0 });
			store.block.put (tx, data.blocks[i]->hash (), *data.blocks[i]);
		}

		for (auto const & [acc, info] : data.accounts)
			store.account.put (tx, acc, info);

		for (auto const & [key, info] : data.pending)
			store.pending.put (tx, key, info);

		for (auto const & [rep, weight] : data.rep_weights)
			store.rep_weight.put (tx, rep, weight);

		for (auto const & [ts, amount] : data.online_weights)
			store.online_weight.put (tx, ts, amount);

		for (auto const & hash : data.pruned)
			store.pruned.put (tx, hash);

		for (auto const & [key, version] : data.peers)
			store.peer.put (tx, key, static_cast<uint32_t> (version));

		for (auto const & [acc, info] : data.confirmation_heights)
			store.confirmation_height.put (tx, acc, info);

		for (auto const & [qroot, hash] : data.final_votes)
			store.final_vote.put (tx, qroot, hash);
	}

	return data;
}
}

TEST (migrations, lmdb_to_rocksdb)
{
	if (nano::default_database_backend () != nano::database_backend::lmdb)
	{
		GTEST_SKIP ();
	}

	nano::logger logger;
	nano::stats stats{ logger };

	auto data_path = nano::unique_path ();

	migration_test_data expected;
	std::map<nano::store::table, size_t> src_counts;

	// Create and populate LMDB store
	{
		auto store = nano::make_store (logger, stats, data_path, nano::dev::constants);

		// Initialize with genesis block
		store->initialize (store->tx_begin_write (), nano::dev::constants);

		// Populate ledger and get expected data
		expected = populate_ledger_for_migration (*store);

		// Record source counts
		auto txn = store->tx_begin_read ();
		for (auto const & [table, name] : store->backend.get_schema ())
		{
			src_counts[table] = store->backend.count_exact (txn, table);
		}
	}

	// Run migration
	ASSERT_NO_THROW (nano::migrate_lmdb_to_rocksdb (data_path));

	// Open RocksDB store and verify
	auto rocksdb_path = nano::database_path_for_backend (data_path, nano::database_backend::rocksdb);
	auto rocksdb_backend = std::make_unique<nano::store::rocksdb::backend_rocksdb> (rocksdb_path, nano::rocksdb_config{}, logger);
	nano::store::ledger_store rocksdb_store{ std::move (rocksdb_backend), nano::store::open_mode::read_only, stats, logger };

	auto txn = rocksdb_store.tx_begin_read ();

	// Verify exact counts match for all tables
	for (auto const & [table, name] : rocksdb_store.backend.get_schema ())
	{
		auto dst_count = rocksdb_store.backend.count_exact (txn, table);
		ASSERT_EQ (dst_count, src_counts[table]) << "Count mismatch for table: " << name;
	}

	// Verify blocks data
	for (auto const & block : expected.blocks)
	{
		auto retrieved = rocksdb_store.block.get (txn, block->hash ());
		ASSERT_NE (retrieved, nullptr) << "Block missing: " << block->hash ().to_string ();
		ASSERT_EQ (*block, *retrieved);
	}

	// Verify accounts data
	for (auto const & [acc, expected_info] : expected.accounts)
	{
		auto info = rocksdb_store.account.get (txn, acc);
		ASSERT_TRUE (info.has_value ()) << "Account missing: " << acc.to_string ();
		ASSERT_EQ (expected_info, *info);
	}

	// Verify pending data
	for (auto const & [key, expected_info] : expected.pending)
	{
		auto info = rocksdb_store.pending.get (txn, key);
		ASSERT_TRUE (info.has_value ()) << "Pending missing";
		ASSERT_EQ (expected_info, *info);
	}

	// Verify rep_weights data
	for (auto const & [rep, expected_weight] : expected.rep_weights)
	{
		auto weight = rocksdb_store.rep_weight.get (txn, rep);
		ASSERT_EQ (expected_weight, weight);
	}

	// Verify confirmation_height data
	for (auto const & [acc, expected_info] : expected.confirmation_heights)
	{
		nano::confirmation_height_info info;
		auto found = !rocksdb_store.confirmation_height.get (txn, acc, info);
		ASSERT_TRUE (found) << "Confirmation height missing for: " << acc.to_string ();
		ASSERT_EQ (expected_info, info);
	}

	// Verify final_votes data
	for (auto const & [qroot, expected_hash] : expected.final_votes)
	{
		auto hash = rocksdb_store.final_vote.get (txn, qroot);
		ASSERT_TRUE (hash.has_value ()) << "Final vote missing";
		ASSERT_EQ (expected_hash, hash.value ());
	}

	// Verify online_weight data
	for (auto const & [ts, expected_amount] : expected.online_weights)
	{
		bool found = false;
		for (auto i = rocksdb_store.online_weight.begin (txn); i != rocksdb_store.online_weight.end (txn); ++i)
		{
			if (i->first == ts)
			{
				ASSERT_EQ (expected_amount, i->second);
				found = true;
				break;
			}
		}
		ASSERT_TRUE (found) << "Online weight missing for timestamp: " << ts;
	}

	// Verify pruned data
	for (auto const & hash : expected.pruned)
	{
		ASSERT_TRUE (rocksdb_store.pruned.exists (txn, hash)) << "Pruned hash missing: " << hash.to_string ();
	}

	// Verify peers data
	for (auto const & [key, expected_version] : expected.peers)
	{
		ASSERT_TRUE (rocksdb_store.peer.exists (txn, key)) << "Peer missing";
	}
}

TEST (migrations, double_migration_fails)
{
	if (nano::default_database_backend () != nano::database_backend::lmdb)
	{
		GTEST_SKIP ();
	}

	nano::logger logger;
	nano::stats stats{ logger };

	auto data_path = nano::unique_path ();
	{
		auto store = nano::make_store (logger, stats, data_path, nano::dev::constants);
	}

	// First migration should succeed
	ASSERT_NO_THROW (nano::migrate_lmdb_to_rocksdb (data_path));

	// Second migration should fail because RocksDB already exists
	ASSERT_THROW (nano::migrate_lmdb_to_rocksdb (data_path), std::runtime_error);
}

TEST (migrations, partial_migration_fails)
{
	if (nano::default_database_backend () != nano::database_backend::lmdb)
	{
		GTEST_SKIP ();
	}

	nano::logger logger;
	nano::stats stats{ logger };

	auto data_path = nano::unique_path ();
	{
		auto store = nano::make_store (logger, stats, data_path, nano::dev::constants);
	}

	// Simulate an aborted migration by creating the RocksDB directory with some files
	auto rocksdb_path = nano::database_path_for_backend (data_path, nano::database_backend::rocksdb);
	std::filesystem::create_directories (rocksdb_path);

	// Create a dummy file to simulate partial/interrupted migration
	std::ofstream (rocksdb_path / "CURRENT") << "dummy";

	// Migration should fail because RocksDB folder already exists (even if partial)
	ASSERT_THROW (nano::migrate_lmdb_to_rocksdb (data_path), std::runtime_error);
}
