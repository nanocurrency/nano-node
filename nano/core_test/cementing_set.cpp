#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/block_context.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/cementing_set.hpp>
#include <nano/node/election.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_cemented.hpp>
#include <nano/store/write_queue.hpp>
#include <nano/test_common/ledger_context.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <future>
#include <thread>

using namespace std::chrono_literals;

namespace
{
/**
 * Standalone harness for the cementing set component.
 * Uses a bare ledger and a non-started ledger_notifications instance, so tests have full control:
 * blocks_processed notifications are fired manually and synchronously from the test thread.
 */
struct test_context
{
	explicit test_context (nano::cementing_set_config config_a = {}, nano::ledger_options ledger_options_a = {}) :
		ledger_ctx{ nano::test::ledger_empty (ledger_options_a) },
		config{ config_a },
		cementing_set{ config, ledger_ctx.ledger (), notifications, ledger_ctx.stats (), ledger_ctx.logger () }
	{
	}

	~test_context ()
	{
		cementing_set.stop ();
	}

	nano::ledger & ledger ()
	{
		return ledger_ctx.ledger ();
	}

	// Allow passing the context directly to helpers expecting a ledger or a work pool
	operator nano::ledger & ()
	{
		return ledger_ctx.ledger ();
	}

	operator nano::work_pool & ()
	{
		return ledger_ctx.pool ();
	}

	nano::test::ledger_context ledger_ctx;
	nano::stats & stats{ ledger_ctx.stats () };
	nano::node_config node_config{};
	nano::ledger_notifications notifications{ node_config, ledger_ctx.stats (), ledger_ctx.logger () };
	nano::cementing_set_config config;
	nano::cementing_set cementing_set;
};

// Builds a chain of state send blocks on the genesis account, each sending 1 raw to a unique account
std::deque<std::shared_ptr<nano::block>> make_chain (nano::work_pool & pool, size_t count)
{
	std::deque<std::shared_ptr<nano::block>> blocks;
	nano::block_builder builder;
	auto previous = nano::dev::genesis->hash ();
	auto balance = nano::dev::constants.genesis_amount;
	for (size_t i = 0; i < count; ++i)
	{
		nano::keypair key;
		balance = balance - 1;
		auto block = builder
					 .state ()
					 .account (nano::dev::genesis_key.pub)
					 .previous (previous)
					 .representative (nano::dev::genesis_key.pub)
					 .balance (balance)
					 .link (key.pub)
					 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
					 .work (*pool.generate (previous))
					 .build ();
		blocks.push_back (block);
		previous = block->hash ();
	}
	return blocks;
}

std::deque<std::shared_ptr<nano::block>> make_chain (nano::test::system & system, size_t count)
{
	return make_chain (system.work, count);
}

void process (nano::ledger & ledger, std::deque<std::shared_ptr<nano::block>> const & blocks)
{
	auto transaction = ledger.tx_begin_write ();
	for (auto const & block : blocks)
	{
		ASSERT_EQ (nano::block_status::progress, ledger.process (transaction, block));
	}
}

void process (nano::node & node, std::deque<std::shared_ptr<nano::block>> const & blocks)
{
	process (node.ledger, blocks);
}

// Thread-safe collector for batch_cemented notifications
struct batch_collector
{
	mutable std::mutex mutex;
	std::deque<nano::cementing_set::context> contexts;
	size_t batches{ 0 };

	void collect (std::deque<nano::cementing_set::context> const & batch)
	{
		std::lock_guard lock{ mutex };
		++batches;
		contexts.insert (contexts.end (), batch.begin (), batch.end ());
	}

	size_t size () const
	{
		std::lock_guard lock{ mutex };
		return contexts.size ();
	}

	size_t batch_count () const
	{
		std::lock_guard lock{ mutex };
		return batches;
	}

	std::deque<nano::cementing_set::context> get () const
	{
		std::lock_guard lock{ mutex };
		return contexts;
	}
};

// Thread-safe collector for hash notifications (already_cemented, cementing_failed)
struct hash_collector
{
	mutable std::mutex mutex;
	std::deque<nano::block_hash> hashes;

	void collect (nano::block_hash const & hash)
	{
		std::lock_guard lock{ mutex };
		hashes.push_back (hash);
	}

	size_t size () const
	{
		std::lock_guard lock{ mutex };
		return hashes.size ();
	}

	std::deque<nano::block_hash> get () const
	{
		std::lock_guard lock{ mutex };
		return hashes;
	}
};
}

/*
 * Component with enable=false never starts processing, but the container interface still works
 */
TEST (cementing_set, disabled)
{
	nano::test::system system;

	nano::cementing_set_config config;
	config.enable = false;
	test_context ctx{ config };

	auto blocks = make_chain (ctx, 1);
	process (ctx, blocks);

	ctx.cementing_set.start (); // No-op

	auto const & hash = blocks.front ()->hash ();
	ASSERT_TRUE (ctx.cementing_set.add (hash));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::insert));

	// Adding the same hash again is rejected
	ASSERT_FALSE (ctx.cementing_set.add (hash));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::duplicate));

	ASSERT_TRUE (ctx.cementing_set.contains (hash));
	ASSERT_FALSE (ctx.cementing_set.contains (nano::block_hash{ 42 }));
	ASSERT_EQ (1, ctx.cementing_set.size ());
	ASSERT_EQ (0, ctx.cementing_set.deferred_size ());

	// Nothing is ever processed
	ASSERT_ALWAYS_EQ (500ms, 0, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cementing));
	ASSERT_TRUE (ctx.cementing_set.contains (hash));
}

/*
 * A single block is cemented and all observers are notified
 */
TEST (cementing_set, cement_single)
{
	nano::test::system system;

	batch_collector collector;
	std::atomic<size_t> cemented_observer_count{ 0 };

	test_context ctx;
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});
	ctx.cementing_set.cemented_observers.add ([&] (auto const &) {
		++cemented_observer_count;
	});

	auto blocks = make_chain (ctx, 1);
	process (ctx, blocks);
	auto block = blocks.front ();

	ASSERT_TRUE (ctx.cementing_set.add (block->hash ()));
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, collector.size (), 1);
	ASSERT_TIMELY_EQ (5s, cemented_observer_count.load (), 1);

	auto contexts = collector.get ();
	ASSERT_EQ (block->hash (), contexts.front ().block->hash ());
	ASSERT_EQ (block->hash (), contexts.front ().confirmation_root);
	ASSERT_EQ (nullptr, contexts.front ().election);

	{
		auto transaction = ctx.ledger ().tx_begin_read ();
		ASSERT_TRUE (ctx.ledger ().cemented.block_exists (transaction, block->hash ()));
	}
	ASSERT_EQ (2, ctx.ledger ().cemented_count ());

	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented_hash));

	// The block is removed from the set once cemented
	ASSERT_TIMELY (5s, !ctx.cementing_set.contains (block->hash ()));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);
	ASSERT_EQ (0, ctx.cementing_set.deferred_size ());
}

/*
 * Cementing the top of a chain implicitly cements all its dependencies, bottom-up
 */
TEST (cementing_set, cement_chain)
{
	nano::test::system system;

	batch_collector collector;
	std::atomic<size_t> cemented_observer_count{ 0 };

	test_context ctx;
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});
	ctx.cementing_set.cemented_observers.add ([&] (auto const &) {
		++cemented_observer_count;
	});

	auto blocks = make_chain (ctx, 5);
	process (ctx, blocks);
	auto const & top = blocks.back ()->hash ();

	ASSERT_TRUE (ctx.cementing_set.add (top));
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, collector.size (), 5);
	ASSERT_TIMELY_EQ (5s, cemented_observer_count.load (), 5);

	// Dependencies are cemented bottom-up, all sharing the requested confirmation root
	auto contexts = collector.get ();
	for (size_t i = 0; i < contexts.size (); ++i)
	{
		ASSERT_EQ (blocks[i]->hash (), contexts[i].block->hash ());
		ASSERT_EQ (top, contexts[i].confirmation_root);
	}

	ASSERT_EQ (5, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented_hash));
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::notify_intermediate));
	ASSERT_EQ (6, ctx.ledger ().cemented_count ());
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);
}

/*
 * Blocks that are already cemented are reported through the already_cemented observer
 */
TEST (cementing_set, already_cemented)
{
	nano::test::system system;

	batch_collector collector;
	hash_collector already;

	test_context ctx;
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});
	ctx.cementing_set.already_cemented.add ([&] (auto const & hashes) {
		for (auto const & hash : hashes)
		{
			already.collect (hash);
		}
	});

	auto blocks = make_chain (ctx, 1);
	process (ctx, blocks);

	// Genesis is already cemented; both entries end up in the same batch
	ASSERT_TRUE (ctx.cementing_set.add (nano::dev::genesis->hash ()));
	ASSERT_TRUE (ctx.cementing_set.add (blocks.front ()->hash ()));
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, already.size (), 1);
	ASSERT_EQ (nano::dev::genesis->hash (), already.get ().front ());
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::already_cemented));

	// The other block is cemented normally
	ASSERT_TIMELY_EQ (5s, collector.size (), 1);
	ASSERT_EQ (blocks.front ()->hash (), collector.get ().front ().block->hash ());

	// Already cemented entries also count as successfully processed
	ASSERT_EQ (2, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented_hash));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);
}

/*
 * With batch_size=1 each entry is processed and notified as a separate batch
 */
TEST (cementing_set, batch_size)
{
	nano::test::system system;

	batch_collector collector;

	nano::cementing_set_config config;
	config.batch_size = 1;
	test_context ctx{ config };
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});

	auto blocks = make_chain (ctx, 3);
	process (ctx, blocks);

	for (auto const & block : blocks)
	{
		ASSERT_TRUE (ctx.cementing_set.add (block->hash ()));
	}
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, collector.size (), 3);
	ASSERT_TIMELY_EQ (5s, collector.batch_count (), 3);

	// Each block is its own confirmation root, processed in insertion order
	auto contexts = collector.get ();
	for (size_t i = 0; i < contexts.size (); ++i)
	{
		ASSERT_EQ (blocks[i]->hash (), contexts[i].block->hash ());
		ASSERT_EQ (blocks[i]->hash (), contexts[i].confirmation_root);
	}

	ASSERT_EQ (3, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented_hash));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);
}

/*
 * Cementing a chain longer than max_blocks issues intermediate notifications to bound memory usage
 */
TEST (cementing_set, max_blocks_intermediate)
{
	nano::test::system system;

	batch_collector collector;

	nano::cementing_set_config config;
	config.max_blocks = 4;
	test_context ctx{ config };
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});

	auto blocks = make_chain (ctx, 10);
	process (ctx, blocks);
	auto const & top = blocks.back ()->hash ();

	ASSERT_TRUE (ctx.cementing_set.add (top));
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, collector.size (), 10);

	// The chain is cemented in max_blocks sized steps: 4 + 4 + 2
	ASSERT_EQ (3, collector.batch_count ());
	ASSERT_EQ (2, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::notify_intermediate));

	// All partial batches share the same confirmation root and arrive bottom-up
	auto contexts = collector.get ();
	for (size_t i = 0; i < contexts.size (); ++i)
	{
		ASSERT_EQ (blocks[i]->hash (), contexts[i].block->hash ());
		ASSERT_EQ (top, contexts[i].confirmation_root);
	}

	ASSERT_EQ (10, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented_hash));
	ASSERT_EQ (11, ctx.ledger ().cemented_count ());
}

/*
 * Blocks missing from the ledger are moved to the deferred set instead of failing outright
 */
TEST (cementing_set, deferred_missing_block)
{
	nano::test::system system;

	test_context ctx;

	// Enough entries to also trigger the queue pressure warning
	size_t const count = 101;
	for (size_t i = 0; i < count; ++i)
	{
		ASSERT_TRUE (ctx.cementing_set.add (nano::block_hash{ i + 1 }));
	}
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), count);
	ASSERT_EQ (count, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::missing_block));
	ASSERT_EQ (count, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cementing_failed));

	// Deferred blocks are reported by contains, but not by size
	ASSERT_TRUE (ctx.cementing_set.contains (nano::block_hash{ 1 }));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);

	// Deferred blocks are kept until requeued or evicted
	ASSERT_ALWAYS_EQ (500ms, count, ctx.cementing_set.deferred_size ());

	// Another missing block while the deferred set is under pressure
	ASSERT_TRUE (ctx.cementing_set.add (nano::block_hash{ count + 1 }));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), count + 1);
}

/*
 * Deferred blocks are requeued and cemented once the missing block is processed by the ledger
 */
TEST (cementing_set, deferred_requeue)
{
	nano::test::system system;

	batch_collector collector;

	test_context ctx;
	ctx.cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});
	ctx.cementing_set.start ();

	// Two blocks, neither in the ledger yet
	auto blocks = make_chain (ctx, 2);
	auto block = blocks.front ();
	auto unrelated = blocks.back ();

	ASSERT_TRUE (ctx.cementing_set.add (block->hash ()));
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), 1);

	// Processing an unrelated block does not requeue anything (handler runs synchronously)
	{
		nano::ledger_notifications::processed_batch_t batch;
		batch.emplace_back (nano::block_status::progress, nano::block_context{ unrelated, nano::block_source::live });
		ctx.notifications.blocks_processed.notify (batch);
	}
	ASSERT_EQ (0, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::requeued));
	ASSERT_EQ (1, ctx.cementing_set.deferred_size ());

	// Once the missing block arrives in the ledger, it is requeued and cemented
	process (ctx, { block });
	{
		nano::ledger_notifications::processed_batch_t batch;
		batch.emplace_back (nano::block_status::progress, nano::block_context{ block, nano::block_source::live });
		ctx.notifications.blocks_processed.notify (batch);
	}
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::requeued));
	ASSERT_EQ (0, ctx.cementing_set.deferred_size ());

	ASSERT_TIMELY_EQ (5s, collector.size (), 1);
	ASSERT_EQ (block->hash (), collector.get ().front ().block->hash ());
	ASSERT_TIMELY (5s, !ctx.cementing_set.contains (block->hash ()));

	auto transaction = ctx.ledger ().tx_begin_read ();
	ASSERT_TRUE (ctx.ledger ().cemented.block_exists (transaction, block->hash ()));
}

/*
 * Deferred blocks past the age cutoff are evicted and reported as failed
 */
TEST (cementing_set, deferred_age_eviction)
{
	nano::test::system system;

	hash_collector failed;

	nano::cementing_set_config config;
	config.deferred_age_cutoff = std::chrono::seconds{ 0 }; // Evict deferred blocks immediately
	test_context ctx{ config };
	ctx.cementing_set.cementing_failed.add ([&] (auto const & hash) {
		failed.collect (hash);
	});

	nano::block_hash const missing{ 1 };
	ASSERT_TRUE (ctx.cementing_set.add (missing));
	ctx.cementing_set.start ();

	ASSERT_TIMELY_EQ (5s, failed.size (), 1);
	ASSERT_EQ (missing, failed.get ().front ());
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::evicted));

	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), 0);
	ASSERT_FALSE (ctx.cementing_set.contains (missing));
}

/*
 * When the deferred set exceeds max_deferred, the oldest entries are evicted first
 */
TEST (cementing_set, deferred_overflow)
{
	nano::test::system system;

	hash_collector failed;

	nano::cementing_set_config config;
	config.max_deferred = 2;
	test_context ctx{ config };
	ctx.cementing_set.cementing_failed.add ([&] (auto const & hash) {
		failed.collect (hash);
	});

	nano::block_hash const missing1{ 1 };
	nano::block_hash const missing2{ 2 };
	nano::block_hash const missing3{ 3 };
	ASSERT_TRUE (ctx.cementing_set.add (missing1));
	ASSERT_TRUE (ctx.cementing_set.add (missing2));
	ASSERT_TRUE (ctx.cementing_set.add (missing3));
	ctx.cementing_set.start ();

	// Only the oldest entry is evicted to get back under the limit
	ASSERT_TIMELY_EQ (5s, failed.size (), 1);
	ASSERT_EQ (missing1, failed.get ().front ());
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::evicted));

	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), 2);
	ASSERT_FALSE (ctx.cementing_set.contains (missing1));
	ASSERT_TRUE (ctx.cementing_set.contains (missing2));
	ASSERT_TRUE (ctx.cementing_set.contains (missing3));
}

/*
 * When notifications queue up faster than they are consumed, cementing cools down and waits
 */
TEST (cementing_set, notification_backpressure)
{
	nano::test::system system;

	batch_collector collector;

	nano::cementing_set_config config;
	config.batch_size = 1;
	config.max_queued_notifications = 1;
	test_context ctx{ config };

	std::promise<void> release_promise;
	auto released = release_promise.get_future ().share ();
	ctx.cementing_set.batch_cemented.add ([&collector, released] (auto const & batch) {
		released.wait (); // Block the notification thread until the test releases it
		collector.collect (batch);
	});

	auto blocks = make_chain (ctx, 2);
	process (ctx, blocks);

	for (auto const & block : blocks)
	{
		ASSERT_TRUE (ctx.cementing_set.add (block->hash ()));
	}
	ctx.cementing_set.start ();

	// First batch occupies the notification thread, second batch hits the cooldown
	ASSERT_TIMELY (5s, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cooldown) >= 1);

	// While cooling down, the second block is already cemented in the ledger but still tracked as being processed
	{
		auto transaction = ctx.ledger ().tx_begin_read ();
		ASSERT_TRUE (ctx.ledger ().cemented.block_exists (transaction, blocks.back ()->hash ()));
	}
	ASSERT_TRUE (ctx.cementing_set.contains (blocks.back ()->hash ()));
	ASSERT_EQ (1, ctx.cementing_set.size ());
	ASSERT_EQ (0, collector.size ()); // No notifications delivered yet

	// Releasing the notification thread drains everything
	release_promise.set_value ();
	ASSERT_TIMELY_EQ (5s, collector.size (), 2);
	ASSERT_TIMELY_EQ (5s, collector.batch_count (), 2);
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.size (), 0);
}

/*
 * Stopping the component aborts long-running cementing gracefully, dropping pending notifications
 */
TEST (cementing_set, stop_during_cementing)
{
	nano::test::system system;

	batch_collector collector;

	nano::cementing_set_config config;
	config.max_blocks = 4;
	config.max_queued_notifications = 1;
	test_context ctx{ config };

	std::promise<void> release_promise;
	auto released = release_promise.get_future ().share ();
	ctx.cementing_set.batch_cemented.add ([&collector, released] (auto const & batch) {
		released.wait (); // Block the notification thread until the test releases it
		collector.collect (batch);
	});

	auto blocks = make_chain (ctx, 20);
	process (ctx, blocks);

	ASSERT_TRUE (ctx.cementing_set.add (blocks.back ()->hash ()));
	ctx.cementing_set.start ();

	// The first intermediate notification blocks the worker, the second parks cementing in the cooldown
	ASSERT_TIMELY (5s, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cooldown) >= 1);

	// Exactly two intermediate steps of 4 blocks are committed at this point
	ASSERT_EQ (9, ctx.ledger ().cemented_count ());

	// Release the notification thread shortly after stop is initiated, so stop can join the workers
	std::thread releaser{ [&release_promise] () {
		std::this_thread::sleep_for (2s);
		release_promise.set_value ();
	} };

	// Stop must return without deadlocking
	ctx.cementing_set.stop ();
	releaser.join ();

	// The interrupted cooldown completes one more cementing round before the stop is noticed at the loop top
	ASSERT_EQ (13, ctx.ledger ().cemented_count ());
	ASSERT_EQ (12, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::cemented));

	// Only the first committed batch was notified, the rest were dropped during shutdown
	ASSERT_TIMELY_EQ (5s, collector.size (), 4);
	ASSERT_EQ (1, collector.batch_count ());
}

/*
 * Hashes of pruned blocks are treated as missing and end up in the deferred set
 */
TEST (cementing_set, add_pruned)
{
	nano::test::system system;

	hash_collector already;

	test_context ctx{ {}, nano::ledger_options{ .enable_pruning = true, .enable_topo_index = false } };
	ctx.cementing_set.already_cemented.add ([&] (auto const & hashes) {
		for (auto const & hash : hashes)
		{
			already.collect (hash);
		}
	});

	auto blocks = make_chain (ctx, 2);
	process (ctx, blocks);
	auto const & pruned_hash = blocks.front ()->hash ();
	auto const & frontier_hash = blocks.back ()->hash ();

	// Cement both blocks and prune the lower one
	{
		auto transaction = ctx.ledger ().tx_begin_write ();
		ctx.ledger ().cement (transaction, frontier_hash);
		ASSERT_EQ (1, ctx.ledger ().pruning_action (transaction, pruned_hash, 1));
		ASSERT_FALSE (ctx.ledger ().any.block_exists (transaction, pruned_hash));
	}

	ASSERT_TRUE (ctx.cementing_set.add (pruned_hash));
	ASSERT_TRUE (ctx.cementing_set.add (frontier_hash));
	ctx.cementing_set.start ();

	// The pruned hash cannot be resolved and is deferred, the existing one is reported as already cemented
	ASSERT_TIMELY_EQ (5s, ctx.cementing_set.deferred_size (), 1);
	ASSERT_TRUE (ctx.cementing_set.contains (pruned_hash));
	ASSERT_EQ (1, ctx.stats.count (nano::stat::type::cementing_set, nano::stat::detail::missing_block));

	ASSERT_TIMELY_EQ (5s, already.size (), 1);
	ASSERT_EQ (frontier_hash, already.get ().front ());
}

/*
 * The tests below run against a full node and cover the wiring between the cementing set, active elections and the confirmation observers.
 */

/*
 * Confirmation observers fire for every block cemented through the node's cementing set
 */
TEST (confirmation_callback, observer_callbacks)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto node = system.add_node (node_config);

	auto blocks = make_chain (system, 2);
	process (*node, blocks);

	node->cementing_set.add (blocks.back ()->hash ());

	// Both blocks are reported as cemented without an associated election
	ASSERT_TIMELY_EQ (5s, 2, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));

	ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_cemented));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

/*
 * Confirmation callbacks and the confirmation history are only updated after the confirmation height write is durable, not when the election reaches quorum
 */
TEST (confirmation_callback, confirmed_history)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	node_config.bootstrap->enable = false;
	auto node = system.add_node (node_config);

	auto blocks = make_chain (system, 2);
	process (*node, blocks);
	auto send = blocks.front ();
	auto send1 = blocks.back ();

	std::shared_ptr<nano::election> election;
	ASSERT_TIMELY (5s, election = nano::test::start_election (system, *node, send1->hash ()));
	{
		// The write guard prevents the cementing thread from making any ledger writes
		auto write_guard = node->store.write_queue.wait (nano::store::writer::testing);

		election->force_confirm ();
		ASSERT_TIMELY_EQ (10s, node->active.size (), 0);
		ASSERT_EQ (0, node->active.recently_cemented.size ());
		ASSERT_TRUE (node->active.empty ());

		// The election is confirmed, but nothing is cemented yet
		auto transaction = node->ledger.tx_begin_read ();
		ASSERT_FALSE (node->ledger.cemented.block_exists (transaction, send->hash ()));

		// The cementing thread is parked in the write queue behind the guard
		ASSERT_TIMELY (10s, node->store.write_queue.contains (nano::store::writer::confirmation_height));

		// No confirmation observers run before the write completes
		ASSERT_ALWAYS_EQ (50ms, 0, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::dir::out));
	}

	ASSERT_TIMELY (10s, !node->store.write_queue.contains (nano::store::writer::confirmation_height));
	ASSERT_TIMELY (5s, node->ledger.cemented.block_exists (node->ledger.tx_begin_read (), send->hash ()));

	// The election winner is reported as confirmed by quorum, the implicitly cemented dependency as inactive
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));

	// Each cemented block ends up in the recently cemented history
	ASSERT_TIMELY_EQ (5s, 2, node->active.recently_cemented.size ());
	ASSERT_TRUE (node->active.empty ());

	ASSERT_EQ (2, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_cemented));
	ASSERT_EQ (3, node->ledger.cemented_count ());
}

/*
 * A dependent election is implicitly confirmed when a successor with quorum cements its chain, and the source election is propagated to the emitted contexts
 */
TEST (confirmation_callback, dependent_election)
{
	nano::test::system system;
	nano::node_config node_config = system.default_config ();
	node_config.backlog_scan->enable = false;
	auto node = system.add_node (node_config);

	batch_collector collector;
	node->cementing_set.batch_cemented.add ([&] (auto const & batch) {
		collector.collect (batch);
	});

	auto blocks = make_chain (system, 3);
	process (*node, blocks);
	auto send1 = blocks[1];
	auto send2 = blocks[2];

	// This election is not confirmed by quorum and should be reported as confirmed by confirmation height
	ASSERT_TRUE (nano::test::start_election (system, *node, send1->hash ()));
	// Confirming this election cements the whole chain
	auto election = nano::test::start_election (system, *node, send2->hash ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();

	ASSERT_TIMELY_EQ (5s, 3, node->stats.count (nano::stat::type::confirmation_height, nano::stat::detail::blocks_cemented));
	// Once the added hash is no longer tracked, cementing of the whole chain has completed
	ASSERT_TIMELY (5s, !node->cementing_set.contains (send2->hash ()));

	// Each block is classified by how its confirmation was triggered
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_quorum, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::active_conf_height, nano::stat::dir::out));
	ASSERT_TIMELY_EQ (5s, 1, node->stats.count (nano::stat::type::confirmation_observer, nano::stat::detail::inactive_conf_height, nano::stat::dir::out));
	ASSERT_EQ (4, node->ledger.cemented_count ());

	// The source election is propagated to every context cemented under its confirmation root
	ASSERT_TIMELY_EQ (5s, collector.size (), 3);
	auto contexts = collector.get ();
	for (size_t i = 0; i < contexts.size (); ++i)
	{
		ASSERT_EQ (blocks[i]->hash (), contexts[i].block->hash ());
		ASSERT_EQ (send2->hash (), contexts[i].confirmation_root);
		ASSERT_EQ (election, contexts[i].election);
	}
}
