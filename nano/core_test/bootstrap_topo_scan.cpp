#include <nano/lib/container_info.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/messages/asc_pull.hpp>
#include <nano/node/bootstrap/topo_scan.hpp>
#include <nano/secure/common.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>

using namespace std::chrono_literals;

namespace
{
struct test_context
{
	nano::stats stats;
	nano::topo_scan_config config;
	nano::bootstrap::topo_scan scan;

	explicit test_context (nano::topo_scan_config config_a = {}) :
		stats{ nano::default_logger () },
		config{ config_a },
		scan{ config, stats }
	{
	}
};

// Number of repair heads currently held, read from the container_info snapshot (the live ground truth).
// The "heads" child holds one entry per head id plus a "frontier" entry; subtract the frontier and the spearhead.
std::size_t repair_head_count (nano::bootstrap::topo_scan const & scan)
{
	auto const info = scan.container_info ();
	for (auto const & [name, child] : info.children ())
	{
		if (name == "heads")
		{
			std::size_t total = 0;
			for (auto const & entry : child.entries ())
			{
				if (entry.name != "frontier")
				{
					++total;
				}
			}
			return total > 0 ? total - 1 : 0; // minus the spearhead (head 0)
		}
	}
	return 0;
}

// Independent restatement of the scaling contract, to assert the engine against (not the engine's own code)
std::size_t expected_repair_heads (nano::topo_scan_config const & config, uint64_t height)
{
	uint64_t const band = config.repair_band_height > 0 ? config.repair_band_height : 1;
	uint64_t const want = (height + band - 1) / band; // ceil
	return std::clamp<uint64_t> (want, config.min_repair_heads, config.max_repair_heads);
}

// Drive the frontier to `height` and return the resulting repair head count
std::size_t heads_at (nano::bootstrap::topo_scan & scan, uint64_t height)
{
	scan.orient (nano::topo_key{ height, nano::block_hash{ 1 } });
	return repair_head_count (scan);
}

nano::topo_key make_topo_key (uint64_t height, uint64_t hash)
{
	return nano::topo_key{ height, nano::block_hash{ hash } };
}
}

/*
 * A fresh engine seeds exactly the floor of repair heads (frontier is zero, so the count clamps to min)
 */
TEST (bootstrap_topo_scan, construction_seeds_floor)
{
	test_context ctx{};
	ASSERT_EQ (repair_head_count (ctx.scan), ctx.config.min_repair_heads);
}

/*
 * The core contract: as the frontier grows, the repair head count tracks ceil (frontier / band) clamped to
 * [min, max]. Asserted against an explicit config so the test is independent of the shipped defaults.
 */
TEST (bootstrap_topo_scan, count_follows_contract)
{
	nano::topo_scan_config config;
	config.repair_band_height = 1'000'000;
	config.min_repair_heads = 2;
	config.max_repair_heads = 10;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	// Frontier only ever moves up (orient takes the max), so feed strictly increasing heights
	uint64_t const heights[] = {
		0, // floor
		500'000, // ceil 1 -> floor 2
		1'000'000, // ceil 1 -> floor 2
		1'000'001, // ceil 2
		5'000'000, // ceil 5
		9'500'000, // ceil 10 -> cap 10
		10'000'000, // ceil 10
		50'000'000, // ceil 50 -> cap 10
	};
	for (auto height : heights)
	{
		ASSERT_EQ (heads_at (scan, height), expected_repair_heads (config, height)) << "frontier=" << height;
	}
}

/*
 * The ceil + clamp behaviour, isolated with small hand-checked numbers and a custom floor/cap
 */
TEST (bootstrap_topo_scan, heads_scale_custom_config)
{
	nano::topo_scan_config config;
	config.repair_band_height = 100;
	config.min_repair_heads = 1;
	config.max_repair_heads = 8;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	ASSERT_EQ (repair_head_count (scan), 1); // floor at frontier 0
	ASSERT_EQ (heads_at (scan, 100), 1); // ceil(100/100) = 1
	ASSERT_EQ (heads_at (scan, 101), 2); // ceil(101/100) = 2
	ASSERT_EQ (heads_at (scan, 250), 3); // ceil(2.5) = 3
	ASSERT_EQ (heads_at (scan, 800), 8); // ceil(8) = 8, hits the cap exactly
	ASSERT_EQ (heads_at (scan, 100'000), 8); // far past the cap -> still 8
}

/*
 * Documents the design intent ("~16 repair heads at the 250M target height") with an explicit config, so it
 * keeps documenting the intent even as the shipped defaults are tuned.
 */
TEST (bootstrap_topo_scan, design_target_16_heads_at_250M)
{
	nano::topo_scan_config config;
	config.repair_band_height = 250'000'000 / 16; // 15'625'000
	config.min_repair_heads = 2;
	config.max_repair_heads = 16;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	ASSERT_EQ (repair_head_count (scan), 2); // floor on a fresh ledger
	ASSERT_EQ (heads_at (scan, 50'000'000), 4);
	ASSERT_EQ (heads_at (scan, 100'000'000), 7);
	ASSERT_EQ (heads_at (scan, 200'000'000), 13);
	ASSERT_EQ (heads_at (scan, 250'000'000), 16); // the target
	ASSERT_EQ (heads_at (scan, 500'000'000), 16); // capped beyond the target, bands grow instead
}

/*
 * The frontier never shrinks within a session, so the repair head count must never shrink either
 */
TEST (bootstrap_topo_scan, heads_never_shrink)
{
	nano::topo_scan_config config;
	config.repair_band_height = 1'000'000;
	config.min_repair_heads = 2;
	config.max_repair_heads = 10;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	ASSERT_EQ (heads_at (scan, 50'000'000), 10); // grown to the cap

	// A lower frontier is ignored (orient takes the max), so the count stays put
	ASSERT_EQ (heads_at (scan, 1'000), 10);
	ASSERT_EQ (heads_at (scan, 0), 10);
}

/*
 * reset () tears the heads back down to the floor
 */
TEST (bootstrap_topo_scan, reset_returns_to_floor)
{
	nano::topo_scan_config config;
	config.repair_band_height = 1'000'000;
	config.min_repair_heads = 3;
	config.max_repair_heads = 12;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	ASSERT_GT (heads_at (scan, 100'000'000), config.min_repair_heads); // grew above the floor
	scan.reset ();
	ASSERT_EQ (repair_head_count (scan), config.min_repair_heads);
}

/*
 * The grow stat increments once per repair head added; since heads are never removed it equals the live count
 */
TEST (bootstrap_topo_scan, grow_stat_tracks_head_count)
{
	nano::topo_scan_config config;
	config.repair_band_height = 1'000'000;
	config.min_repair_heads = 2;
	config.max_repair_heads = 10;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	// The constructor's reset () already seeded the floor, emitting one grow per seeded head
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_scan, nano::stat::detail::grow), config.min_repair_heads);

	heads_at (scan, 100'000'000); // grow to the cap
	// Heads are never removed, so the cumulative grow count equals the live repair head count
	ASSERT_EQ (ctx.stats.count (nano::stat::type::bootstrap_topo_scan, nano::stat::detail::grow), repair_head_count (scan));
	ASSERT_EQ (repair_head_count (scan), config.max_repair_heads);
}

/*
 * orient () restarts the spearhead at the monotonic target and drops stale replies.
 */
TEST (bootstrap_topo_scan, orient_restarts_spearhead_round_at_monotonic_target)
{
	nano::topo_scan_config config;
	config.consideration_count = 2;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	auto const target = make_topo_key (20, 0);
	auto const stale_candidate = make_topo_key (10, 20);

	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 2);
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));

	scan.orient (target);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, target);
	ASSERT_EQ (req->fanout, 2);
	ASSERT_TRUE (req->exclude.empty ());

	scan.process (1, { cursor, stale_candidate });
	ASSERT_TRUE (pages.empty ());
}

/*
 * orient () never moves the spearhead cursor behind the current frontier.
 */
TEST (bootstrap_topo_scan, orient_does_not_move_spearhead_backwards)
{
	nano::topo_scan_config config;
	config.consideration_count = 1;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	auto const high = make_topo_key (100, 10);
	auto const low = make_topo_key (10, 10);

	scan.orient (high);
	scan.orient (low);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, high);
}

/*
 * orient () disarms repair heads so their bands are recomputed from the new frontier.
 */
TEST (bootstrap_topo_scan, orient_reinitializes_repair_head_ranges)
{
	nano::topo_scan_config config;
	config.min_repair_heads = 2;
	config.max_repair_heads = 2;
	config.repair_consideration = 1;
	config.redundant_skip_stride = 25;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	auto const now = std::chrono::steady_clock::now ();
	scan.orient (make_topo_key (100, 1));

	std::optional<nano::bootstrap::topo_scan::request> old_trailing_head;
	for (auto i = 0; i < 2; ++i)
	{
		auto req = scan.next ({ .include_spearhead = false, .include_repair = true }, now);
		ASSERT_TRUE (req);
		if (req->head == 1)
		{
			old_trailing_head = req;
		}
	}
	ASSERT_TRUE (old_trailing_head);
	ASSERT_EQ (old_trailing_head->start, make_topo_key (75, 0));
	ASSERT_TRUE (scan.dispatch (old_trailing_head->head, old_trailing_head->start, 1, nano::account{ 1 }));

	scan.orient (make_topo_key (200, 1));

	std::optional<nano::bootstrap::topo_scan::request> new_trailing_head;
	for (auto i = 0; i < 2; ++i)
	{
		auto req = scan.next ({ .include_spearhead = false, .include_repair = true }, now + 1ms);
		ASSERT_TRUE (req);
		if (req->head == 1)
		{
			new_trailing_head = req;
		}
	}
	ASSERT_TRUE (new_trailing_head);
	ASSERT_EQ (new_trailing_head->start, make_topo_key (175, 0));
	ASSERT_TRUE (new_trailing_head->exclude.empty ());
}

/*
 * A scan request always asks for the protocol page limit, but only retires the smallest configured number of
 * usable new entries. The next cursor is the last retired entry, not the largest entry returned by the peer.
 */
TEST (bootstrap_topo_scan, requests_protocol_max_and_trims_new_candidates)
{
	nano::topo_scan_config config;
	config.consideration_count = 1;
	config.candidates = 2;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	auto const b = make_topo_key (10, 30);
	auto const c = make_topo_key (10, 40);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->count, nano::messages::asc_pull_ack::topo_index_payload::max_entries);
	ASSERT_EQ (req->fanout, 1);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	scan.process (1, { cursor, a, b, c });

	ASSERT_EQ (pages.size (), 1);
	ASSERT_EQ (pages.front ().entries.size (), 2);
	ASSERT_EQ (pages.front ().entries[0], a);
	ASSERT_EQ (pages.front ().entries[1], b);

	req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, b);
}

/*
 * With redundant replies, the spearhead advances only to the furthest candidate that has enough peer support.
 * A minority-only tail is left for a later round so one peer cannot move the cursor past unseen keys.
 */
TEST (bootstrap_topo_scan, spearhead_advances_only_to_supported_boundary)
{
	nano::topo_scan_config config;
	config.consideration_count = 3;
	config.candidates = 4;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	auto const b = make_topo_key (10, 30);
	auto const c = make_topo_key (10, 40);
	auto const d = make_topo_key (10, 50);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 3);
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 2, nano::account{ 2 }));
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 3, nano::account{ 3 }));

	scan.process (1, { cursor, a, b, c });
	scan.process (2, { cursor, a, b, c });
	scan.process (3, { cursor, a, b, d });

	ASSERT_EQ (pages.size (), 1);
	ASSERT_EQ (pages.front ().entries.size (), 3);
	ASSERT_EQ (pages.front ().entries[0], a);
	ASSERT_EQ (pages.front ().entries[1], b);
	ASSERT_EQ (pages.front ().entries[2], c);

	req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, c);
}

/*
 * After a round makes real progress, the scan clears the pacing timestamp and immediately issues the next
 * cursor. This lets the spearhead chase a discovered frontier without waiting for the per-head cooldown.
 */
TEST (bootstrap_topo_scan, progress_refires_without_cooldown)
{
	nano::topo_scan_config config;
	config.consideration_count = 1;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	scan.process (1, { cursor, a });
	ASSERT_EQ (pages.size (), 1);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, a);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * A partially sampled active round does not wait for cooldown before topping up. The next request asks only
 * for the remaining fanout and excludes peers already sampled for the same cursor.
 */
TEST (bootstrap_topo_scan, partial_round_topup_ignores_cooldown)
{
	nano::topo_scan_config config;
	config.consideration_count = 3;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 3);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 2);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), nano::account{ 1 });
}

/*
 * A full round with useful partial replies waits for cooldown before re-polling. The cooldown retry keeps
 * the previous candidates and exclusions, then advances once the extra peer supplies enough support.
 */
TEST (bootstrap_topo_scan, cooldown_topup_keeps_round_progress)
{
	nano::topo_scan_config config;
	config.consideration_count = 2;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 2);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 2, nano::account{ 2 }));
	scan.process (1, { cursor, a });
	ASSERT_TRUE (pages.empty ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_FALSE (req);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + config.cooldown + 1ms);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_EQ (req->exclude.size (), 2);
	ASSERT_NE (std::find (req->exclude.begin (), req->exclude.end (), nano::account{ 1 }), req->exclude.end ());
	ASSERT_NE (std::find (req->exclude.begin (), req->exclude.end (), nano::account{ 2 }), req->exclude.end ());

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 3, nano::account{ 3 }));
	scan.process (3, { cursor, a });

	ASSERT_EQ (pages.size (), 1);
	ASSERT_EQ (pages.front ().entries.size (), 1);
	ASSERT_EQ (pages.front ().entries.front (), a);
	ASSERT_EQ (pages.front ().redundancy, 2);
}

/*
 * An empty completed round means the cursor has nothing new right now. The round state is cleared, but the
 * timestamp is preserved so the same cursor is not sampled again until cooldown expires.
 */
TEST (bootstrap_topo_scan, empty_response_resets_round_and_keeps_cooldown)
{
	nano::topo_scan_config config;
	config.consideration_count = 1;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 1);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	scan.process (1, { cursor });
	ASSERT_TRUE (pages.empty ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_FALSE (req);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + config.cooldown + 1ms);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * If the only sampled peer is cancelled, the head has no active round left. Clearing the timestamp lets the
 * next call retry immediately from the same cursor, with no stale exclusion carried over.
 */
TEST (bootstrap_topo_scan, cancel_last_sample_retries_without_cooldown)
{
	nano::topo_scan_config config;
	config.consideration_count = 2;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 2);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	scan.cancel (1);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 2);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * Exhausting the peer pool before any peer is sampled should mark the scan as starved and wait for cooldown.
 * On retry, the exhausted flag is cleared and the cursor can complete normally without restarting in maybe_advance().
 */
TEST (bootstrap_topo_scan, zero_peer_exhaustion_retries_without_restart)
{
	nano::topo_scan_config config;
	config.consideration_count = 2;
	config.cooldown = 10ms;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const now = std::chrono::steady_clock::now ();
	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false }, now);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 2);

	scan.exhausted (req->head, req->start);
	ASSERT_TRUE (scan.starved ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + (config.cooldown / 2));
	ASSERT_FALSE (req);

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + config.cooldown + 1ms);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 2);
	ASSERT_TRUE (req->exclude.empty ());
	ASSERT_FALSE (scan.starved ());

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));
	scan.process (1, { cursor, a });
	ASSERT_TRUE (pages.empty ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false }, now + config.cooldown + 1ms);
	ASSERT_TRUE (req);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), nano::account{ 1 });

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 2, nano::account{ 2 }));
	scan.process (2, { cursor, a });

	ASSERT_EQ (pages.size (), 1);
	ASSERT_EQ (pages.front ().entries.size (), 1);
	ASSERT_EQ (pages.front ().entries.front (), a);
	ASSERT_EQ (pages.front ().redundancy, 2);
}

/*
 * orient () discards in-flight reservations even when the cursor value is unchanged.
 */
TEST (bootstrap_topo_scan, orient_same_cursor_discards_inflight_reservations)
{
	nano::topo_scan_config config;
	config.consideration_count = 1;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, nano::account{ 1 }));

	scan.orient (cursor);
	scan.process (1, { cursor, a });
	ASSERT_TRUE (pages.empty ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_TRUE (req->exclude.empty ());
}

/*
 * dispatch () rejects duplicate peer samples for the same cursor.
 */
TEST (bootstrap_topo_scan, duplicate_peer_dispatch_is_rejected)
{
	nano::topo_scan_config config;
	config.consideration_count = 2;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	std::deque<nano::bootstrap::topo_scan::page> pages;
	scan.sink = [&pages] (nano::bootstrap::topo_scan::page page) {
		pages.push_back (std::move (page));
	};

	auto const cursor = make_topo_key (10, 10);
	auto const a = make_topo_key (10, 20);
	auto const peer1 = nano::account{ 1 };
	auto const peer2 = nano::account{ 2 };
	scan.orient (cursor);

	auto req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_TRUE (scan.dispatch (req->head, req->start, 1, peer1));
	ASSERT_FALSE (scan.dispatch (req->head, req->start, 2, peer1));

	scan.process (2, { cursor, a });
	ASSERT_TRUE (pages.empty ());

	scan.process (1, { cursor, a });
	ASSERT_TRUE (pages.empty ());

	req = scan.next ({ .include_spearhead = true, .include_repair = false });
	ASSERT_TRUE (req);
	ASSERT_EQ (req->start, cursor);
	ASSERT_EQ (req->fanout, 1);
	ASSERT_EQ (req->exclude.size (), 1);
	ASSERT_EQ (req->exclude.front (), peer1);

	ASSERT_TRUE (scan.dispatch (req->head, req->start, 3, peer2));
	scan.process (3, { cursor, a });

	ASSERT_EQ (pages.size (), 1);
	ASSERT_EQ (pages.front ().entries.size (), 1);
	ASSERT_EQ (pages.front ().entries.front (), a);
	ASSERT_EQ (pages.front ().redundancy, 2);
}

/*
 * Repair heads arm onto the trailing band first, then partition the broad range.
 */
TEST (bootstrap_topo_scan, repair_heads_start_on_expected_bands)
{
	nano::topo_scan_config config;
	config.min_repair_heads = 3;
	config.max_repair_heads = 3;
	config.repair_consideration = 1;
	config.redundant_skip_stride = 20;
	test_context ctx{ config };
	auto & scan = ctx.scan;

	auto const now = std::chrono::steady_clock::now ();
	scan.orient (make_topo_key (100, 1));

	std::optional<nano::bootstrap::topo_scan::request> trailing;
	std::optional<nano::bootstrap::topo_scan::request> broad0;
	std::optional<nano::bootstrap::topo_scan::request> broad1;
	for (auto i = 0; i < 3; ++i)
	{
		auto req = scan.next ({ .include_spearhead = false, .include_repair = true }, now);
		ASSERT_TRUE (req);
		if (req->head == 1)
		{
			trailing = req;
		}
		else if (req->head == 2)
		{
			broad0 = req;
		}
		else if (req->head == 3)
		{
			broad1 = req;
		}
	}

	ASSERT_TRUE (trailing);
	ASSERT_TRUE (broad0);
	ASSERT_TRUE (broad1);
	ASSERT_EQ (trailing->start, make_topo_key (80, 0));
	ASSERT_EQ (broad0->start, make_topo_key (1, 0));
	ASSERT_EQ (broad1->start, make_topo_key (51, 0));
}
