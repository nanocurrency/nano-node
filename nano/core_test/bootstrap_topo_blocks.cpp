#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/bootstrap/topo_blocks.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <memory>

using namespace std::chrono_literals;

namespace
{
struct test_context
{
	nano::logger & logger;
	nano::stats stats;
	nano::topo_scan_config config;
	nano::bootstrap::topo_blocks blocks;

	test_context () :
		logger{ nano::default_logger () },
		stats{ logger },
		blocks{ config, stats, logger }
	{
		config.fetch_cooldown = 2s;
		config.max_fetch_attempts = 10;
	}
};

nano::topo_key make_topo_key (uint64_t height, uint64_t hash)
{
	return nano::topo_key{ height, nano::block_hash{ hash } };
}

nano::topo_key make_topo_key (uint64_t height, nano::block_hash const & hash)
{
	return nano::topo_key{ height, hash };
}

std::shared_ptr<nano::block> make_block (uint64_t seed)
{
	nano::keypair key;
	nano::block_builder builder;
	return builder
	.send ()
	.previous (seed)
	.destination (seed + 1000)
	.balance (1000000 - seed)
	.sign (key.prv, key.pub)
	.work (seed + 2000)
	.build ();
}
}

/*
 * Duplicate topo entries are stored once and fetched in topo order.
 */
TEST (bootstrap_topo_blocks, add_deduplicates_and_orders_pending_entries)
{
	test_context ctx;
	auto const key1 = make_topo_key (100, 1);
	auto const key2 = make_topo_key (101, 2);
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key2, key1, key2 });
	ASSERT_EQ (ctx.blocks.pending_count (), 2);
	ASSERT_EQ (ctx.blocks.total_count (), 2);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::pending), 2);

	ctx.blocks.add ({ key1 });
	ASSERT_EQ (ctx.blocks.pending_count (), 2);
	ASSERT_EQ (ctx.blocks.total_count (), 2);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::pending), 2);

	auto req = ctx.blocks.next (10, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 2);
	ASSERT_EQ (req->hashes[0], key1.hash);
	ASSERT_EQ (req->hashes[1], key2.hash);
}

/*
 * next () returns at most the requested number of hashes.
 */
TEST (bootstrap_topo_blocks, next_respects_batch_limit)
{
	test_context ctx;
	auto const key1 = make_topo_key (100, 1);
	auto const key2 = make_topo_key (101, 2);
	auto const key3 = make_topo_key (102, 3);
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key1, key2, key3 });

	auto req = ctx.blocks.next (0, now);
	ASSERT_FALSE (req);

	req = ctx.blocks.next (2, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 2);
	ASSERT_EQ (req->hashes[0], key1.hash);
	ASSERT_EQ (req->hashes[1], key2.hash);
}

/*
 * Retried fetches exclude peers already sampled for the entry.
 */
TEST (bootstrap_topo_blocks, next_excludes_sampled_peer)
{
	test_context ctx;
	auto const key = make_topo_key (100, 1);
	auto const peer1 = nano::account{ 1 };
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key.hash);
	ASSERT_TRUE (req->exclude.empty ());

	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, peer1, now));
	ctx.blocks.process (1, {});

	req = ctx.blocks.next (1, now + ctx.config.fetch_cooldown);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key.hash);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), peer1);
}

/*
 * Dispatch fails once every hash in the request is no longer pending.
 */
TEST (bootstrap_topo_blocks, dispatch_rejects_request_when_all_entries_are_stale)
{
	test_context ctx;
	auto block = make_block (1);
	auto const key = make_topo_key (100, block->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ctx.blocks.process (1, { block });
	ASSERT_EQ (ctx.blocks.pending_count (), 0);

	ASSERT_FALSE (ctx.blocks.dispatch (*req, 2, nano::account{ 2 }, now));
	ctx.blocks.process (2, { block });
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::fetched), 1);
}

/*
 * Fetch responses only satisfy entries matching tracked hashes.
 */
TEST (bootstrap_topo_blocks, process_ignores_untracked_blocks)
{
	test_context ctx;
	auto block = make_block (1);
	auto unrelated = make_block (2);
	auto const key = make_topo_key (100, block->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ctx.blocks.process (1, { unrelated });
	ASSERT_EQ (ctx.blocks.pending_count (), 1);
	ASSERT_FALSE (ctx.blocks.has_submittable ());
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::fetched), 0);

	req = ctx.blocks.next (1, now + ctx.config.fetch_cooldown);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key.hash);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), nano::account{ 1 });

	ASSERT_TRUE (ctx.blocks.dispatch (*req, 2, nano::account{ 2 }, now + ctx.config.fetch_cooldown));
	ctx.blocks.process (2, { block });
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_TRUE (ctx.blocks.has_submittable ());
}

/*
 * Submission stops at the first pending gap in topo order.
 */
TEST (bootstrap_topo_blocks, next_submit_releases_only_contiguous_fetched_prefix)
{
	test_context ctx;
	auto block1 = make_block (1);
	auto block2 = make_block (2);
	auto block3 = make_block (3);
	auto const key1 = make_topo_key (100, block1->hash ());
	auto const key2 = make_topo_key (101, block2->hash ());
	auto const key3 = make_topo_key (102, block3->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key3, key1, key2 });

	auto req = ctx.blocks.next (3, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ctx.blocks.process (1, { block3 });

	req = ctx.blocks.next (3, now + ctx.config.fetch_cooldown);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 2, nano::account{ 2 }, now + ctx.config.fetch_cooldown));
	ctx.blocks.process (2, { block1 });

	auto batch = ctx.blocks.next_submit (10);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch.front ()->hash (), block1->hash ());
	ASSERT_EQ (ctx.blocks.total_count (), 2);
	ASSERT_FALSE (ctx.blocks.has_submittable ());

	req = ctx.blocks.next (3, now + (ctx.config.fetch_cooldown * 2));
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key2.hash);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 3, nano::account{ 3 }, now + (ctx.config.fetch_cooldown * 2)));
	ctx.blocks.process (3, { block2 });

	batch = ctx.blocks.next_submit (10);
	ASSERT_EQ (batch.size (), 2);
	ASSERT_EQ (batch[0]->hash (), block2->hash ());
	ASSERT_EQ (batch[1]->hash (), block3->hash ());
	ASSERT_EQ (ctx.blocks.total_count (), 0);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
}

/*
 * next_submit () releases no more than the requested batch size.
 */
TEST (bootstrap_topo_blocks, next_submit_respects_max_batch)
{
	test_context ctx;
	auto block1 = make_block (1);
	auto block2 = make_block (2);
	auto const key1 = make_topo_key (100, block1->hash ());
	auto const key2 = make_topo_key (101, block2->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key1, key2 });

	auto req = ctx.blocks.next (2, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ASSERT_EQ (ctx.blocks.inflight_count (), 2);
	ctx.blocks.process (1, { block1, block2 });
	ASSERT_EQ (ctx.blocks.inflight_count (), 0);

	auto batch = ctx.blocks.next_submit (1);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch.front ()->hash (), block1->hash ());
	ASSERT_EQ (ctx.blocks.total_count (), 1);
	ASSERT_TRUE (ctx.blocks.has_submittable ());

	batch = ctx.blocks.next_submit (1);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch.front ()->hash (), block2->hash ());
	ASSERT_EQ (ctx.blocks.total_count (), 0);
}

/*
 * A skipped gap can be rediscovered and retried from a clean state.
 */
TEST (bootstrap_topo_blocks, skipped_entry_can_be_rediscovered_and_rearmed)
{
	test_context ctx;
	ctx.config.max_fetch_attempts = 1;
	ctx.config.fetch_cooldown = 0ms;
	auto const key = make_topo_key (100, 1);
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ctx.blocks.process (1, {});
	ASSERT_EQ (ctx.blocks.pending_count (), 1);

	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_EQ (ctx.blocks.total_count (), 1);
	ASSERT_TRUE (ctx.blocks.has_submittable ());
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 1);

	ctx.blocks.add ({ key });
	ASSERT_EQ (ctx.blocks.pending_count (), 1);
	ASSERT_EQ (ctx.blocks.total_count (), 1);
	ASSERT_FALSE (ctx.blocks.has_submittable ());

	req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key.hash);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * Reaching the attempt limit does not discard a block whose response is still in flight.
 */
TEST (bootstrap_topo_blocks, attempt_limit_waits_for_inflight_response)
{
	test_context ctx;
	ctx.config.max_fetch_attempts = 2;
	ctx.config.fetch_cooldown = 0ms;
	auto block = make_block (1);
	auto const key = make_topo_key (100, block->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 2, nano::account{ 2 }, now));
	ASSERT_EQ (ctx.blocks.inflight_count (), 2);

	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_EQ (ctx.blocks.pending_count (), 1);
	ASSERT_FALSE (ctx.blocks.has_submittable ());
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 0);

	ctx.blocks.cancel (1);
	ASSERT_EQ (ctx.blocks.inflight_count (), 1);
	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_EQ (ctx.blocks.pending_count (), 1);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 0);

	ctx.blocks.process (2, { block });
	ASSERT_EQ (ctx.blocks.inflight_count (), 0);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_TRUE (ctx.blocks.has_submittable ());
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::fetched), 1);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 0);

	auto batch = ctx.blocks.next_submit (1);
	ASSERT_EQ (batch.size (), 1);
	ASSERT_EQ (batch.front ()->hash (), block->hash ());
}

/*
 * A capped entry remains pending until its final in-flight request is cancelled.
 */
TEST (bootstrap_topo_blocks, attempt_limit_waits_for_inflight_cancellation)
{
	test_context ctx;
	ctx.config.max_fetch_attempts = 1;
	ctx.config.fetch_cooldown = 0ms;
	auto const key = make_topo_key (100, 1);
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));

	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_EQ (ctx.blocks.pending_count (), 1);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 0);

	ctx.blocks.cancel (1);
	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_TRUE (ctx.blocks.has_submittable ());
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_fetch, nano::stat::detail::skip), 1);
}

/*
 * Skipped gaps advance the submit cursor without returning a block.
 */
TEST (bootstrap_topo_blocks, skipped_gap_is_consumed_by_submit_cursor)
{
	test_context ctx;
	ctx.config.max_fetch_attempts = 1;
	ctx.config.fetch_cooldown = 0ms;
	auto const key = make_topo_key (100, 1);
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));
	ctx.blocks.process (1, {});

	req = ctx.blocks.next (1, now);
	ASSERT_FALSE (req);
	ASSERT_TRUE (ctx.blocks.has_submittable ());

	auto batch = ctx.blocks.next_submit (10);
	ASSERT_TRUE (batch.empty ());
	ASSERT_EQ (ctx.blocks.total_count (), 0);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_submit, nano::stat::detail::gap), 1);
}

/*
 * reset () clears pending entries and makes old reservations harmless.
 */
TEST (bootstrap_topo_blocks, reset_clears_entries_and_inflight)
{
	test_context ctx;
	auto block = make_block (1);
	auto const key = make_topo_key (100, block->hash ());
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });
	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, nano::account{ 1 }, now));

	ctx.blocks.reset ();
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_EQ (ctx.blocks.total_count (), 0);
	ASSERT_FALSE (ctx.blocks.has_submittable ());
	ASSERT_FALSE (ctx.blocks.next (1, now));

	ctx.blocks.process (1, { block });
	ctx.blocks.cancel (1);
	ASSERT_EQ (ctx.blocks.pending_count (), 0);
	ASSERT_EQ (ctx.blocks.total_count (), 0);
}

/*
 * Exhausting the peer pool clears exclusions for a fresh fetch sweep.
 */
TEST (bootstrap_topo_blocks, exhausted_resets_sampled_peers)
{
	test_context ctx;
	auto const key = make_topo_key (100, 1);
	auto const peer1 = nano::account{ 1 };
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, peer1, now));
	ctx.blocks.process (1, {});

	req = ctx.blocks.next (1, now + ctx.config.fetch_cooldown);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->exclude.size (), 1);

	ctx.blocks.exhausted (*req);

	req = ctx.blocks.next (1, now + ctx.config.fetch_cooldown);
	ASSERT_TRUE (req);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * Cancelled fetches retry immediately while preserving the sampled-peer exclusion.
 */
TEST (bootstrap_topo_blocks, cancel_rearms_without_resampling_peer)
{
	test_context ctx;
	auto const key = make_topo_key (100, 1);
	auto const peer1 = nano::account{ 1 };
	auto const now = std::chrono::steady_clock::now ();

	ctx.blocks.add ({ key });

	auto req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_TRUE (ctx.blocks.dispatch (*req, 1, peer1, now));

	ctx.blocks.cancel (1);

	req = ctx.blocks.next (1, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->hashes.size (), 1);
	ASSERT_EQ (req->hashes.front (), key.hash);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), peer1);
}
