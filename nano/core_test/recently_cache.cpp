#include <nano/lib/blockbuilders.hpp>
#include <nano/node/election_status.hpp>
#include <nano/node/recently_cemented_cache.hpp>
#include <nano/node/recently_confirmed_cache.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
std::shared_ptr<nano::block> make_test_block ()
{
	nano::block_builder builder;
	return builder.state ()
	.account (nano::dev::genesis_key.pub)
	.previous (nano::test::random_hash ())
	.representative (nano::dev::genesis_key.pub)
	.balance (nano::dev::constants.genesis_amount - 1)
	.link (nano::dev::genesis_key.pub)
	.sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
	.work (0)
	.build ();
}

nano::election_status make_test_election_status (std::chrono::milliseconds duration = std::chrono::milliseconds (100))
{
	auto block = make_test_block ();
	nano::election_status status;
	status.winner = block;
	status.type = nano::election_status_type::active_confirmed_quorum;
	status.election_end = std::chrono::system_clock::now ();
	status.election_duration = duration;
	return status;
}
}

/*
 * recently_confirmed_cache
 */

TEST (recently_confirmed_cache, construction)
{
	nano::recently_confirmed_cache cache (10);
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_confirmed_cache, put)
{
	nano::recently_confirmed_cache cache (3);
	std::vector<nano::qualified_root> roots;
	std::vector<nano::block_hash> hashes;

	// Add entries and test size limit
	for (int i = 0; i < 5; ++i)
	{
		auto root = nano::test::random_qualified_root ();
		auto hash = nano::test::random_hash ();
		roots.push_back (root);
		hashes.push_back (hash);
		cache.put (root, hash, {});
	}

	ASSERT_EQ (3, cache.size ());

	// Test duplicate filtering
	cache.put (roots[4], hashes[4], {});
	ASSERT_EQ (3, cache.size ());

	// First entries should have been evicted (LRU)
	ASSERT_FALSE (cache.contains (roots[0]));
	ASSERT_FALSE (cache.contains (roots[1]));

	// Last entries should still be present
	ASSERT_TRUE (cache.contains (roots[2]));
	ASSERT_TRUE (cache.contains (roots[3]));
	ASSERT_TRUE (cache.contains (roots[4]));
}

TEST (recently_confirmed_cache, erase)
{
	nano::recently_confirmed_cache cache (10);
	auto root = nano::test::random_qualified_root ();
	auto hash = nano::test::random_hash ();

	cache.put (root, hash, {});
	ASSERT_TRUE (cache.contains (hash));
	ASSERT_EQ (1, cache.size ());

	cache.erase (hash);
	ASSERT_FALSE (cache.contains (hash));
	ASSERT_FALSE (cache.contains (root));
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_confirmed_cache, clear)
{
	nano::recently_confirmed_cache cache (10);
	for (int i = 0; i < 5; ++i)
	{
		auto root = nano::test::random_qualified_root ();
		auto hash = nano::test::random_hash ();
		cache.put (root, hash, {});
	}

	ASSERT_EQ (5, cache.size ());
	cache.clear ();
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_confirmed_cache, latency_percentiles_empty)
{
	nano::recently_confirmed_cache cache (10);
	auto stats = cache.latency_percentiles ();
	ASSERT_EQ (0, stats.p50);
	ASSERT_EQ (0, stats.p90);
	ASSERT_EQ (0, stats.p99);
}

TEST (recently_confirmed_cache, latency_percentiles_single)
{
	nano::recently_confirmed_cache cache (10);
	auto status = make_test_election_status (std::chrono::milliseconds (500));
	cache.put (status.winner->qualified_root (), status.winner->hash (), status);

	auto stats = cache.latency_percentiles ();
	ASSERT_EQ (500, stats.p50);
	ASSERT_EQ (500, stats.p90);
	ASSERT_EQ (500, stats.p99);
}

TEST (recently_confirmed_cache, latency_percentiles_distribution)
{
	nano::recently_confirmed_cache cache (1000);
	for (int i = 1; i <= 100; ++i)
	{
		auto status = make_test_election_status (std::chrono::milliseconds (i));
		cache.put (status.winner->qualified_root (), status.winner->hash (), status);
	}

	auto stats = cache.latency_percentiles ();
	ASSERT_EQ (50, stats.p50);
	ASSERT_EQ (90, stats.p90);
	ASSERT_EQ (99, stats.p99);
}

TEST (recently_confirmed_cache, latency_percentiles_outliers)
{
	nano::recently_confirmed_cache cache (1000);

	// 9 fast confirmations + 1 slow outlier (10 total)
	for (int i = 0; i < 9; ++i)
	{
		auto status = make_test_election_status (std::chrono::milliseconds (10));
		cache.put (status.winner->qualified_root (), status.winner->hash (), status);
	}
	auto status = make_test_election_status (std::chrono::milliseconds (10000));
	cache.put (status.winner->qualified_root (), status.winner->hash (), status);

	auto stats = cache.latency_percentiles ();
	// p50 and p90 should reflect the fast majority
	ASSERT_EQ (10, stats.p50);
	ASSERT_EQ (10, stats.p90);
	// p99 with 10 entries: ceil(0.99*10)-1 = 9, captures the outlier
	ASSERT_EQ (10000, stats.p99);
}

/*
 * recently_cemented_cache
 */

TEST (recently_cemented_cache, construction)
{
	nano::recently_cemented_cache cache (10);
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_cemented_cache, put)
{
	nano::recently_cemented_cache cache (3);
	std::vector<nano::election_status> statuses;

	// Add entries and test size limit
	for (int i = 0; i < 5; ++i)
	{
		auto status = make_test_election_status ();
		statuses.push_back (status);
		cache.put (status);
	}

	ASSERT_EQ (3, cache.size ());

	// Test duplicate filtering
	cache.put (statuses[4]);
	ASSERT_EQ (3, cache.size ());

	// First entries should have been evicted (LRU)
	ASSERT_FALSE (cache.contains (statuses[0].winner->qualified_root ()));
	ASSERT_FALSE (cache.contains (statuses[1].winner->qualified_root ()));

	// Last entries should still be present
	ASSERT_TRUE (cache.contains (statuses[2].winner->qualified_root ()));
	ASSERT_TRUE (cache.contains (statuses[3].winner->qualified_root ()));
	ASSERT_TRUE (cache.contains (statuses[4].winner->qualified_root ()));
}

TEST (recently_cemented_cache, erase)
{
	nano::recently_cemented_cache cache (10);
	auto status = make_test_election_status ();

	cache.put (status);
	ASSERT_TRUE (cache.contains (status.winner->hash ()));
	ASSERT_EQ (1, cache.size ());

	cache.erase (status.winner->hash ());
	ASSERT_FALSE (cache.contains (status.winner->hash ()));
	ASSERT_FALSE (cache.contains (status.winner->qualified_root ()));
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_cemented_cache, clear)
{
	nano::recently_cemented_cache cache (10);
	for (int i = 0; i < 5; ++i)
	{
		auto status = make_test_election_status ();
		cache.put (status);
	}

	ASSERT_EQ (5, cache.size ());
	cache.clear ();
	ASSERT_EQ (0, cache.size ());
}

TEST (recently_cemented_cache, list)
{
	nano::recently_cemented_cache cache (10);

	// Test empty list
	auto list = cache.list ();
	ASSERT_TRUE (list.empty ());

	// Add entries and test list functionality
	std::vector<nano::election_status> statuses;
	for (int i = 0; i < 5; ++i)
	{
		auto status = make_test_election_status ();
		statuses.push_back (status);
		cache.put (status);
	}

	// Test full list
	list = cache.list ();
	ASSERT_EQ (5, list.size ());

	// List should be in reverse order (most recent first)
	for (size_t i = 0; i < list.size (); ++i)
	{
		ASSERT_EQ (statuses[4 - i].winner->hash (), list[i].winner->hash ());
	}

	// Test list with limit
	auto limited_list = cache.list (3);
	ASSERT_EQ (3, limited_list.size ());
}