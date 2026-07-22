#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/bootstrap_service.hpp>
#include <nano/node/bootstrap/database_scan_index.hpp>
#include <nano/node/bootstrap/queries.hpp>
#include <nano/node/make_store.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/hinted.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <map>
#include <sstream>

using namespace std::chrono_literals;

/**
 * Tests the base case for returning
 */
TEST (bootstrap, account_base)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

/**
 * Tests that bootstrap will return multiple new blocks in-order
 */
TEST (bootstrap, account_inductive)
{
	nano::node_flags flags;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();

	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	ASSERT_EQ (nano::block_status::progress, node0.process (send2));

	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (50s, node1.block (send2->hash ()) != nullptr);
}

/**
 * Tests that bootstrap will return multiple new blocks in-order
 */
TEST (bootstrap, trace_base)
{
	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	nano::test::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::keypair key;
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto receive1 = builder.make_block ()
					.account (key.pub)
					.previous (0)
					.representative (nano::dev::genesis_key.pub)
					.link (send1->hash ())
					.balance (1)
					.sign (key.prv, key.pub)
					.work (*system.work.generate (key.pub))
					.build ();
	auto & node1 = *system.add_node ();

	ASSERT_EQ (nano::block_status::progress, node0.process (send1));
	ASSERT_EQ (nano::block_status::progress, node0.process (receive1));

	ASSERT_TIMELY (10s, node1.ledger.any.receivable_upper_bound (node1.ledger.tx_begin_read (), key.pub, 0) != node1.ledger.any.receivable_end ());
	ASSERT_TIMELY (10s, node1.block (receive1->hash ()) != nullptr);
}

/*
 * Tests that the priority strategy drops blocks already present in the ledger with a read
 * transaction before submitting them to the (more expensive) block processor.
 */
TEST (bootstrap, priority_filters_known_blocks)
{
	nano::test::system system;

	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;

	nano::node_config config;
	// Isolate the priority strategy from the other block-producing strategies
	config.bootstrap->enable_database_scan = false;
	config.bootstrap->enable_dependency_walker = false;
	config.bootstrap->enable_frontier_scan = false;
	config.bootstrap->enable_topo_scan = false;
	// Force safe requests (start from the confirmed frontier) so responses include blocks we
	// already hold above the confirmed frontier
	config.bootstrap->optimistic_request_percentage = 0;
	// Re-sample the account without cooldown, so the test's pace doesn't depend on the
	// (worker-deferred) post-response timestamp reset
	config.bootstrap->account_sets.cooldown = std::chrono::milliseconds{ 0 };
	// No elections, so the seeded block stays present-but-unconfirmed
	config.backlog_scan->enable = false;
	config.priority_scheduler->enable = false;
	config.optimistic_scheduler->enable = false;
	config.hinted_scheduler->enable = false;

	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();

	// Seed both nodes with send1 (present but unconfirmed) before they start. node1's confirmed
	// frontier therefore stays at genesis while its head advances to send1.
	system.set_initialization_blocks ({ send1 });

	auto & node0 = *system.add_node (config, flags);
	auto & node1 = *system.add_node (config, flags);

	// The priority strategy repeatedly safe-requests the genesis account from its confirmed
	// frontier. The response is [genesis, send1]: genesis is the echoed cursor and send1 is
	// already in node1's ledger, so the early filter drops it instead of submitting it.
	ASSERT_TIMELY (10s, node1.stats.count (nano::stat::type::bootstrap, nano::stat::detail::filtered_blocks) >= 3);
}

/*
 * Tests that bootstrap will prioritize existing accounts with outdated frontiers
 */
TEST (bootstrap, frontier_scan)
{
	nano::test::system system;

	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	flags.disable_elections = true; // Disable election schedulers
	nano::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap->enable_priorities = false;
	config.bootstrap->enable_dependency_walker = false;
	config.bootstrap->enable_topo_scan = false;
	// Disable election activation
	config.backlog_scan->enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens -> 10 updates)
	std::vector<std::shared_ptr<nano::block>> sends;
	std::vector<std::shared_ptr<nano::block>> opens;
	std::vector<std::shared_ptr<nano::block>> updates;
	{
		auto source = nano::dev::genesis_key;
		auto latest = nano::dev::genesis->hash ();
		auto balance = nano::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			nano::keypair key;
			nano::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			auto update = builder
						  .state ()
						  .account (key.pub)
						  .previous (open->hash ())
						  .representative (0)
						  .balance (1)
						  .link (0)
						  .sign (key.prv, key.pub)
						  .work (*system.work.generate (open->hash ()))
						  .build ();

			sends.push_back (send);
			opens.push_back (open);
			updates.push_back (update);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<nano::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	blocks.insert (blocks.end (), opens.begin (), opens.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (nano::test::process (node0, updates));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should detect all the accounts with missing blocks
	ASSERT_TIMELY (10s, std::all_of (updates.begin (), updates.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}

/*
 * Tests that bootstrap will prioritize not yet existing accounts with pending blocks
 */
TEST (bootstrap, frontier_scan_pending)
{
	nano::test::system system;

	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	flags.disable_elections = true; // Disable election schedulers
	nano::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap->enable_priorities = false;
	config.bootstrap->enable_dependency_walker = false;
	config.bootstrap->enable_topo_scan = false;
	// Disable election activation
	config.backlog_scan->enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens)
	std::vector<std::shared_ptr<nano::block>> sends;
	std::vector<std::shared_ptr<nano::block>> opens;
	{
		auto source = nano::dev::genesis_key;
		auto latest = nano::dev::genesis->hash ();
		auto balance = nano::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			nano::keypair key;
			nano::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			sends.push_back (send);
			opens.push_back (open);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<nano::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (nano::test::process (node0, opens));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should detect all the accounts with missing blocks
	ASSERT_TIMELY (10s, std::all_of (opens.begin (), opens.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}

/*
 * Bootstrap should not attempt to prioritize accounts that can't be immediately connected to the ledger (no pending blocks, no existing frontier)
 */
TEST (bootstrap, frontier_scan_cannot_prioritize)
{
	nano::test::system system;

	nano::node_flags flags;
	flags.disable_legacy_bootstrap = true;
	flags.disable_elections = true; // Disable election schedulers
	nano::node_config config;
	// Disable other bootstrap strategies
	config.bootstrap->enable_priorities = false;
	config.bootstrap->enable_dependency_walker = false;
	config.bootstrap->enable_topo_scan = false;
	// Disable election activation
	config.backlog_scan->enable = false;

	// Prepare blocks for frontier scan (genesis 10 sends -> 10 opens -> 10 sends -> 10 opens)
	std::vector<std::shared_ptr<nano::block>> sends;
	std::vector<std::shared_ptr<nano::block>> opens;
	std::vector<std::shared_ptr<nano::block>> sends2;
	std::vector<std::shared_ptr<nano::block>> opens2;
	{
		auto source = nano::dev::genesis_key;
		auto latest = nano::dev::genesis->hash ();
		auto balance = nano::dev::genesis->balance ().number ();

		size_t const count = 10;

		for (int n = 0; n < count; ++n)
		{
			nano::keypair key, key2;
			nano::block_builder builder;

			balance -= 1;
			auto send = builder
						.state ()
						.account (source.pub)
						.previous (latest)
						.representative (source.pub)
						.balance (balance)
						.link (key.pub)
						.sign (source.prv, source.pub)
						.work (*system.work.generate (latest))
						.build ();

			latest = send->hash ();

			auto open = builder
						.state ()
						.account (key.pub)
						.previous (0)
						.representative (key.pub)
						.balance (1)
						.link (send->hash ())
						.sign (key.prv, key.pub)
						.work (*system.work.generate (key.pub))
						.build ();

			auto send2 = builder
						 .state ()
						 .account (key.pub)
						 .previous (open->hash ())
						 .representative (key.pub)
						 .balance (0)
						 .link (key2.pub)
						 .sign (key.prv, key.pub)
						 .work (*system.work.generate (open->hash ()))
						 .build ();

			auto open2 = builder
						 .state ()
						 .account (key2.pub)
						 .previous (0)
						 .representative (key2.pub)
						 .balance (1)
						 .link (send2->hash ())
						 .sign (key2.prv, key2.pub)
						 .work (*system.work.generate (key2.pub))
						 .build ();

			sends.push_back (send);
			opens.push_back (open);
			sends2.push_back (send2);
			opens2.push_back (open2);
		}
	}

	// Initialize nodes with blocks without the `updates` frontiers
	std::vector<std::shared_ptr<nano::block>> blocks;
	blocks.insert (blocks.end (), sends.begin (), sends.end ());
	blocks.insert (blocks.end (), opens.begin (), opens.end ());
	system.set_initialization_blocks ({ blocks.begin (), blocks.end () });

	auto & node0 = *system.add_node (config, flags);
	ASSERT_TRUE (nano::test::process (node0, sends2));
	ASSERT_TRUE (nano::test::process (node0, opens2));

	// No blocks should be broadcast to the other node
	auto & node1 = *system.add_node (config, flags);
	ASSERT_ALWAYS_EQ (100ms, node1.ledger.block_count (), blocks.size () + 1);

	// Frontier scan should not detect the accounts
	ASSERT_ALWAYS (1s, std::none_of (opens2.begin (), opens2.end (), [&node1] (auto const & block) {
		return node1.bootstrap.prioritized (block->account ());
	}));
}

/*
 * Tests that bootstrap.reset() properly recovers when called during an ongoing bootstrap
 * This mimics node restart behaviour so this also ensures that the bootstrap is able to recover from a node restart
 */
TEST (bootstrap, reset)
{
	nano::test::system system;

	// Test configuration
	int const chain_count = 10;
	int const blocks_per_chain = 5;

	nano::node_flags flags;
	flags.disable_elections = true; // Disable election schedulers
	nano::node_config config;
	// Disable election activation
	config.backlog_scan->enable = false;
	// Add request limits to slow down bootstrap
	config.bootstrap->priority_rate_limit = 30;
	config.bootstrap->topo_rate_limit = 30;
	config.bootstrap->topo_scan.fetch_batch = 1;

	// Start server node
	auto & node_server = *system.add_node (config, flags);

	// Create multiple chains of blocks
	auto chains = nano::test::setup_chains (system, node_server, chain_count, blocks_per_chain);

	int const total_blocks = node_server.block_count ();
	int const halfway_blocks = total_blocks / 2;

	// Start client node and begin bootstrap
	auto & node_client = *system.add_node (config, flags);
	ASSERT_LE (node_client.block_count (), halfway_blocks); // Should not be synced yet

	// Wait until bootstrap has started and processed some blocks but not all
	// Target approximately halfway through
	ASSERT_TIMELY (15s, node_client.block_count () >= halfway_blocks);
	ASSERT_LT (node_client.block_count (), total_blocks);

	// Reset bootstrap halfway through the process
	node_client.bootstrap.reset ();

	// Bootstrap should automatically restart and eventually sync all blocks
	ASSERT_TIMELY_EQ (30s, node_client.block_count (), total_blocks);
}

/*
 * Tests that the database scan always produces safe pull queries:
 * - for accounts with a confirmation height, the pull starts from the confirmed frontier (not the unconfirmed head)
 * - for accounts without a confirmation height, the pull starts from the account root
 */
TEST (bootstrap, database_scan_safe_queries)
{
	nano::node_flags flags;
	flags.disable_elections = true; // Disable election schedulers
	nano::node_config config;
	// Keep confirmation under manual control so the head can differ from the confirmed frontier
	config.backlog_scan->enable = false;

	nano::test::system system;
	auto & node = *system.add_node (config, flags);

	nano::keypair key;
	nano::state_block_builder builder;

	// Genesis chain: send1 -> send2 -> send3, where send3 funds a new account `key`
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build ();
	auto send2 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 2)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build ();
	auto send3 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (key.pub)
				 .balance (nano::dev::constants.genesis_amount - 3)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();
	auto open = builder.make_block ()
				.account (key.pub)
				.previous (0)
				.representative (key.pub)
				.link (send3->hash ())
				.balance (1)
				.sign (key.prv, key.pub)
				.work (*system.work.generate (key.pub))
				.build ();

	ASSERT_EQ (nano::block_status::progress, node.process (send1));
	ASSERT_EQ (nano::block_status::progress, node.process (send2));
	ASSERT_EQ (nano::block_status::progress, node.process (send3));
	ASSERT_EQ (nano::block_status::progress, node.process (open));

	// Confirm the genesis chain only up to send1, leaving send2/send3 unconfirmed (head != confirmed frontier)
	// The `key` account is left entirely unconfirmed (no confirmation height)
	nano::test::confirm (node.ledger, send1);

	nano::bootstrap::database_scan_index db_scan{ node.ledger };

	// Sweep the ledger and collect one query per account
	std::map<nano::account, nano::bootstrap::blocks_query> queries;
	for (int i = 0; i < 100 && queries.size () < 2; ++i)
	{
		auto query = db_scan.next ([] (nano::account const &) { return true; });
		if (!query)
		{
			break;
		}
		queries[query->account] = *query;
	}

	// Genesis: safe request starts from the confirmed frontier (send1), NOT the unconfirmed head (send3)
	ASSERT_TRUE (queries.contains (nano::dev::genesis_key.pub));
	auto const & genesis_query = queries.at (nano::dev::genesis_key.pub);
	ASSERT_EQ (genesis_query.type, nano::bootstrap::query_type::blocks_by_hash);
	ASSERT_EQ (genesis_query.start.as_block_hash (), send1->hash ());
	ASSERT_NE (genesis_query.start.as_block_hash (), send3->hash ());
	ASSERT_EQ (genesis_query.count, 2);

	// Unconfirmed account: pull starts from the account root
	ASSERT_TRUE (queries.contains (key.pub));
	auto const & key_query = queries.at (key.pub);
	ASSERT_EQ (key_query.type, nano::bootstrap::query_type::blocks_by_account);
	ASSERT_EQ (key_query.start.as_account (), key.pub);
	ASSERT_EQ (key_query.count, 2);
}
