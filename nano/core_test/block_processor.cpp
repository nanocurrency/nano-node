#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/node/block_processor.hpp>
#include <nano/node/node.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/test_common/chains.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <atomic>

using namespace std::chrono_literals;

// Async add of a single block with callback, verifies processing and callback invocation
TEST (block_processor, add)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;

	nano::keypair key;
	nano::block_builder builder;
	balance -= nano::Knano_ratio;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (latest)
				.representative (nano::dev::genesis_key.pub)
				.balance (balance)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();

	std::atomic<bool> callback_called{ false };
	std::atomic<nano::block_status> callback_status{};
	auto added = node.block_processor.add (send, nano::block_source::live, nullptr, [&] (nano::block_status status) {
		callback_status = status;
		callback_called = true;
	});
	ASSERT_TRUE (added);

	// Callback should fire once the block is processed
	ASSERT_TIMELY (5s, callback_called.load ());
	ASSERT_EQ (callback_status.load (), nano::block_status::progress);
	ASSERT_TRUE (node.block_or_pruned_exists (send->hash ()));
	ASSERT_EQ (node.ledger.block_count (), 2);
}

// Block with invalid work is rejected before queueing
TEST (block_processor, add_insufficient_work)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	nano::keypair key;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (0)
				.build ();

	// Block should be rejected without entering the queue
	auto added = node.block_processor.add (send, nano::block_source::live);
	ASSERT_FALSE (added);
	ASSERT_EQ (node.stats.count (nano::stat::type::block_processor, nano::stat::detail::insufficient_work), 1);
	ASSERT_EQ (node.ledger.block_count (), 1);
}

// Synchronous add blocks the caller until processing completes, also verifies duplicate block returns old status
TEST (block_processor, add_blocking)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;

	nano::keypair key;
	nano::block_builder builder;
	balance -= nano::Knano_ratio;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (latest)
				.representative (nano::dev::genesis_key.pub)
				.balance (balance)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (latest))
				.build ();

	// Blocks caller until result is available
	auto result = node.block_processor.add_blocking (send, nano::block_source::local);
	ASSERT_TRUE (result.has_value ());
	ASSERT_EQ (result.value (), nano::block_status::progress);
	ASSERT_TRUE (node.block_or_pruned_exists (send->hash ()));

	// Processing the same block again should return old
	auto result2 = node.block_processor.add_blocking (send, nano::block_source::local);
	ASSERT_TRUE (result2.has_value ());
	ASSERT_EQ (result2.value (), nano::block_status::old);
}

// When queue is full, add_blocking returns nullopt because the promise is destroyed unfulfilled
TEST (block_processor, add_blocking_overfill)
{
	nano::test::system system;

	nano::node_config node_config;
	// Queue size 0 guarantees immediate rejection
	node_config.block_processor.max_system_queue = 0;
	auto & node = *system.add_node (node_config);

	nano::keypair key;
	nano::block_builder builder;
	auto send = builder
				.state ()
				.account (nano::dev::genesis_key.pub)
				.previous (nano::dev::genesis->hash ())
				.representative (nano::dev::genesis_key.pub)
				.balance (nano::dev::constants.genesis_amount - nano::Knano_ratio)
				.link (key.pub)
				.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				.work (*system.work.generate (nano::dev::genesis->hash ()))
				.build ();

	// Block is dropped due to queue being full, promise is destroyed unfulfilled, add_blocking returns nullopt
	auto result = node.block_processor.add_blocking (send, nano::block_source::local);
	ASSERT_FALSE (result.has_value ());
	ASSERT_EQ (node.stats.count (nano::stat::type::block_processor, nano::stat::detail::overfill), 1);
}

// Batch add of a block chain with callback on the last block
TEST (block_processor, add_many)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;

	std::deque<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < 5; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;
		balance -= nano::Knano_ratio;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		latest = send->hash ();
		blocks.push_back (send);
	}

	std::atomic<bool> callback_called{ false };
	std::atomic<nano::block_status> callback_status{};

	// All blocks queued under a single lock, callback attached to the last block
	auto added = node.block_processor.add_many (blocks, nano::block_source::live, nullptr, [&] (nano::block_status status) {
		callback_status = status;
		callback_called = true;
	});
	ASSERT_EQ (added, 5);

	// Callback fires when the last block in the batch is processed
	ASSERT_TIMELY (5s, callback_called.load ());
	ASSERT_EQ (callback_status.load (), nano::block_status::progress);
	ASSERT_EQ (node.ledger.block_count (), 6);
	for (auto const & block : blocks)
	{
		ASSERT_TRUE (node.block_or_pruned_exists (block->hash ()));
	}
}

// Batch add with limited queue size, some blocks are dropped due to overfill
TEST (block_processor, add_many_overfill)
{
	nano::test::system system;

	nano::node_config node_config;
	// Only 4 blocks fit in the system queue, remaining will be dropped
	node_config.block_processor.max_system_queue = 4;
	auto & node = *system.add_node (node_config);

	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;

	std::deque<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < 8; ++n)
	{
		nano::keypair key;
		nano::block_builder builder;
		balance -= nano::Knano_ratio;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (key.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();
		latest = send->hash ();
		blocks.push_back (send);
	}

	// Not all blocks should fit, overfill stat tracks the dropped ones
	auto added = node.block_processor.add_many (blocks, nano::block_source::local);
	ASSERT_LT (added, 8);
	ASSERT_GT (node.stats.count (nano::stat::type::block_processor, nano::stat::detail::overfill), 0);
}

// Batch add with a mix of valid and invalid work blocks, only valid ones are queued
TEST (block_processor, add_many_mixed_work)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	auto latest = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;

	// Use deterministic keys so block hashes (and therefore roots) are fixed across runs
	nano::keypair key1{ "1" };
	nano::keypair key2{ "2" };
	nano::keypair key3{ "3" };
	nano::block_builder builder;

	// Block 1: valid work
	balance -= nano::Knano_ratio;
	auto send1 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (latest)
				 .representative (nano::dev::genesis_key.pub)
				 .balance (balance)
				 .link (key1.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (latest))
				 .build ();

	// Block 2: invalid work (will be skipped)
	balance -= nano::Knano_ratio;
	auto send2 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (balance)
				 .link (key2.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (0)
				 .build ();

	// Block 3: valid work (will hit gap_previous since block 2 was skipped)
	balance -= nano::Knano_ratio;
	auto send3 = builder
				 .state ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (send2->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .balance (balance)
				 .link (key3.pub)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (send2->hash ()))
				 .build ();

	auto added = node.block_processor.add_many ({ send1, send2, send3 }, nano::block_source::live);

	// Only 2 blocks should be queued (send2 has invalid work)
	ASSERT_EQ (added, 2);
	ASSERT_EQ (node.stats.count (nano::stat::type::block_processor, nano::stat::detail::insufficient_work), 1);

	// Block 1 should be processed, block 2 was never queued
	ASSERT_TIMELY (5s, node.block_or_pruned_exists (send1->hash ()));
	ASSERT_FALSE (node.block_or_pruned_exists (send2->hash ()));
}

// Batch add with empty deque is a no-op
TEST (block_processor, add_many_empty)
{
	nano::test::system system;
	auto & node = *system.add_node ();

	std::deque<std::shared_ptr<nano::block>> empty;
	auto added = node.block_processor.add_many (empty, nano::block_source::live);
	ASSERT_EQ (added, 0);
	ASSERT_EQ (node.ledger.block_count (), 1);
}

TEST (block_processor, backlog_throttling)
{
	nano::test::system system;

	nano::node_config node_config;
	// Backlog won't be rolled back, as we want to test the throttling works when backlog is exceeded
	node_config.max_backlog = 5;
	node_config.backlog_scan.enable = false;
	node_config.bounded_backlog.enable = false; // Disable rollbacks
	// Allow at most 4 blocks per second when throttling
	node_config.block_processor.batch_size = 1;
	node_config.block_processor.backlog_throttle = 500ms;
	auto & node = *system.add_node (node_config);

	const int howmany_blocks = 2;
	const int howmany_chains = 5;

	auto chains = nano::test::setup_chains (system, node, howmany_chains, howmany_blocks, nano::dev::genesis_key, /* do not confirm */ false);

	ASSERT_TIMELY_EQ (20s, node.ledger.block_count (), 21);

	// Prepare live spam blocks
	int const spam_count = 20;

	auto latest = node.latest (nano::dev::genesis_key.pub);
	auto balance = node.balance (nano::dev::genesis_key.pub);

	std::vector<std::shared_ptr<nano::block>> blocks;
	for (int n = 0; n < spam_count; ++n)
	{
		nano::keypair throwaway;
		nano::block_builder builder;

		balance -= 1;
		auto send = builder
					.state ()
					.account (nano::dev::genesis_key.pub)
					.previous (latest)
					.representative (nano::dev::genesis_key.pub)
					.balance (balance)
					.link (throwaway.pub)
					.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					.work (*system.work.generate (latest))
					.build ();

		latest = send->hash ();

		blocks.push_back (send);
	}

	// Send them to the node as live blocks
	nano::test::process_live (node, blocks);

	// Ensure no more than 10 blocks are processed in 5 seconds
	ASSERT_NEVER (5s, node.ledger.block_count () > 21 + 10);
}