#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/account_info.hpp>
#include <nano/secure/pending_info.hpp>
#include <nano/store/crawler.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger_store.hpp>
#include <nano/test_common/make_store.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <unordered_set>
#include <vector>

namespace
{
// Get sequential_attempts threshold from crawler
constexpr size_t sequential_threshold = nano::store::crawler<nano::store::ledger::account_view, nano::store::read_transaction>::sequential_attempts;

// Helper to populate accounts with given keys
void populate_accounts (nano::store::ledger_store & store, std::vector<nano::uint256_t> const & keys)
{
	auto txn = store.tx_begin_write ();
	for (auto const & key : keys)
	{
		store.account.put (txn, nano::account{ key }, nano::account_info{});
	}
}

// Helper to populate pending entries: vector of (account, list of hashes)
void populate_pending (nano::store::ledger_store & store, std::vector<std::pair<nano::uint256_t, std::vector<nano::uint256_t>>> const & entries)
{
	auto txn = store.tx_begin_write ();
	for (auto const & [account, hashes] : entries)
	{
		for (auto const & hash : hashes)
		{
			store.pending.put (txn, nano::pending_key{ nano::account{ account }, nano::block_hash{ hash } }, nano::pending_info{});
		}
	}
}
}

/*
 * Construction & validity
 */

TEST (crawler, construct_with_data)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 10 });
}

TEST (crawler, construct_at_exact_key)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 20 });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 20 });
}

TEST (crawler, construct_between_keys)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 15 });

	// Should position at first key >= 15, which is 30
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 30 });
}

TEST (crawler, construct_beyond_range)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ std::numeric_limits<nano::uint256_t>::max () });

	ASSERT_FALSE (crawler);
}

TEST (crawler, empty_db)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// Crawler on empty DB should be invalid
	ASSERT_FALSE (crawler);

	// Operations on invalid crawler should be safe
	ASSERT_FALSE (crawler.next_entry ());
	ASSERT_FALSE (crawler.next_group ());
	ASSERT_EQ (crawler.find (nano::account{ 100 }), nullptr);
	ASSERT_EQ (crawler.find_group (nano::account{ 100 }), nullptr);
	ASSERT_FALSE (crawler);
}

/*
 * Access
 */

TEST (crawler, dereference_operators)
{
	auto store = nano::test::make_store ();

	// Create account with specific info
	nano::account test_account{ 42 };
	nano::account_info info;
	info.balance = nano::amount{ 1000 };
	info.block_count = 5;

	{
		auto txn = store->tx_begin_write ();
		store->account.put (txn, test_account, info);
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler);

	// Test operator*
	auto const & value = *crawler;
	ASSERT_EQ (value.first, test_account);
	ASSERT_EQ (value.second.balance, nano::amount{ 1000 });
	ASSERT_EQ (value.second.block_count, 5);

	// Test operator->
	ASSERT_EQ (crawler->first, test_account);
	ASSERT_EQ (crawler->second.balance, nano::amount{ 1000 });
}

TEST (crawler, key_simple)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 42 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// For simple keys the full key and the group key coincide
	ASSERT_EQ (crawler.key (), nano::account{ 42 });
	ASSERT_EQ (crawler.group_key (), nano::account{ 42 });
}

TEST (crawler, key_compound)
{
	auto store = nano::test::make_store ();

	populate_pending (*store, { { 5, { 7 } } });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	// key() returns the full compound key, group_key() only the account prefix
	ASSERT_EQ (crawler.key ().account, nano::account{ 5 });
	ASSERT_EQ (crawler.key ().hash, nano::block_hash{ 7 });
	ASSERT_EQ (crawler.group_key (), nano::account{ 5 });
}

/*
 * Entry advancement
 */

TEST (crawler, operator_increment)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_EQ (crawler.key (), nano::account{ 10 });

	auto & result = ++crawler;
	ASSERT_EQ (&result, &crawler); // Returns reference to self
	ASSERT_EQ (crawler.key (), nano::account{ 20 });

	++crawler;
	ASSERT_EQ (crawler.key (), nano::account{ 30 });

	++crawler;
	ASSERT_FALSE (crawler);
}

TEST (crawler, next_entry_returns_validity)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_TRUE (crawler.next_entry ());
	ASSERT_EQ (crawler.key (), nano::account{ 20 });

	ASSERT_FALSE (crawler.next_entry ());
	ASSERT_FALSE (crawler);
}

TEST (crawler, entry_iteration_within_group)
{
	auto store = nano::test::make_store ();

	populate_pending (*store, {
							  { 1, { 10, 20 } },
							  { 2, { 30 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	// operator++ advances one raw entry, visiting every entry of a group
	ASSERT_EQ (crawler.key ().account, nano::account{ 1 });
	ASSERT_EQ (crawler.key ().hash, nano::block_hash{ 10 });

	++crawler;
	ASSERT_EQ (crawler.key ().account, nano::account{ 1 });
	ASSERT_EQ (crawler.key ().hash, nano::block_hash{ 20 });

	++crawler;
	ASSERT_EQ (crawler.key ().account, nano::account{ 2 });
	ASSERT_EQ (crawler.key ().hash, nano::block_hash{ 30 });

	++crawler;
	ASSERT_FALSE (crawler);
}

TEST (crawler, iteration_order_ascending)
{
	auto store = nano::test::make_store ();

	// Insert in non-sorted order
	populate_accounts (*store, { 50, 10, 30, 20, 40 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	std::vector<nano::uint256_t> order;
	for (; crawler; ++crawler)
	{
		order.push_back (crawler.key ().number ());
	}

	ASSERT_EQ (order, (std::vector<nano::uint256_t>{ 10, 20, 30, 40, 50 }));
}

/*
 * Group advancement
 */

TEST (crawler, next_group_simple)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// For simple keys every entry is its own group
	ASSERT_TRUE (crawler.next_group ());
	ASSERT_EQ (crawler.key (), nano::account{ 20 });

	ASSERT_TRUE (crawler.next_group ());
	ASSERT_EQ (crawler.key (), nano::account{ 30 });

	ASSERT_FALSE (crawler.next_group ());
	ASSERT_FALSE (crawler);
}

TEST (crawler, next_group_compound)
{
	auto store = nano::test::make_store ();

	// Account 1: 3 pending entries
	// Account 2: 2 pending entries
	// Account 3: 1 pending entry
	populate_pending (*store, {
							  { 1, { 10, 20, 30 } },
							  { 2, { 10, 20 } },
							  { 3, { 10 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	ASSERT_EQ (crawler.group_key (), nano::account{ 1 });

	// next_group() should skip to account 2, not to the next entry of account 1
	ASSERT_TRUE (crawler.next_group ());
	ASSERT_EQ (crawler.group_key (), nano::account{ 2 });

	ASSERT_TRUE (crawler.next_group ());
	ASSERT_EQ (crawler.group_key (), nano::account{ 3 });

	ASSERT_FALSE (crawler.next_group ());
	ASSERT_FALSE (crawler);
}

TEST (crawler, next_group_seek_fallback)
{
	auto store = nano::test::make_store ();

	// Account 1 with more entries than sequential_threshold to trigger seek fallback
	std::vector<nano::uint256_t> hashes;
	for (size_t i = 1; i <= sequential_threshold + 1; ++i)
	{
		hashes.push_back (i);
	}
	populate_pending (*store, {
							  { 1, hashes },
							  { 2, { 1 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	ASSERT_EQ (crawler.group_key (), nano::account{ 1 });

	// next_group() exhausts the sequential window and falls back to seek
	ASSERT_TRUE (crawler.next_group ());
	ASSERT_EQ (crawler.group_key (), nano::account{ 2 });

	ASSERT_FALSE (crawler.next_group ());
}

TEST (crawler, next_group_saturation)
{
	auto store = nano::test::make_store ();

	auto max_val = std::numeric_limits<nano::uint256_t>::max ();

	// More entries than sequential_threshold at the max account
	// This forces next_group() to exhaust sequential iteration and hit the saturation check
	{
		auto txn = store->tx_begin_write ();
		for (size_t i = 1; i <= sequential_threshold + 1; ++i)
		{
			store->pending.put (txn,
			nano::pending_key{ nano::account{ max_val }, nano::block_hash{ i } },
			nano::pending_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn, nano::account{ max_val });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.group_key ().number (), max_val);

	// No group past the max account is possible, saturation must move to end
	ASSERT_FALSE (crawler.next_group ());
	ASSERT_FALSE (crawler);
}

/*
 * find & find_group - forward-only probing
 */

TEST (crawler, find_exact)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	auto const * found = crawler.find (nano::account{ 20 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ 20 });
	ASSERT_EQ (crawler.key (), nano::account{ 20 }); // Crawler parked on the match
}

TEST (crawler, find_current)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// Probing the current entry matches without moving
	auto const * found = crawler.find (nano::account{ 10 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ 10 });
}

TEST (crawler, find_miss_parks_ahead)
{
	auto store = nano::test::make_store ();

	// Large gaps between keys
	populate_accounts (*store, { 1, 1000, 1000000 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// A miss leaves the crawler at the first entry past the target
	ASSERT_EQ (crawler.find (nano::account{ 500 }), nullptr);
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 1000 });

	// The parked entry is still findable
	ASSERT_NE (crawler.find (nano::account{ 1000 }), nullptr);

	ASSERT_EQ (crawler.find (nano::account{ 500000 }), nullptr);
	ASSERT_EQ (crawler.key (), nano::account{ 1000000 });
}

TEST (crawler, find_miss_past_end)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	ASSERT_EQ (crawler.find (nano::account{ 100 }), nullptr);
	ASSERT_FALSE (crawler);

	// Probing an exhausted crawler stays safe
	ASSERT_EQ (crawler.find (nano::account{ 200 }), nullptr);
	ASSERT_FALSE (crawler);
}

TEST (crawler, find_backward_target)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 30 });

	// Probes are forward-only: entries behind the crawler are never found
	ASSERT_EQ (crawler.find (nano::account{ 10 }), nullptr);
	ASSERT_EQ (crawler.key (), nano::account{ 30 }); // Crawler did not move

	ASSERT_NE (crawler.find (nano::account{ 30 }), nullptr);
}

TEST (crawler, find_compound)
{
	auto store = nano::test::make_store ();

	populate_pending (*store, {
							  { 10, { 1, 2, 3 } },
							  { 20, { 1 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	// Full-key probe can target an entry in the middle of a group
	auto const * found = crawler.find (nano::pending_key{ nano::account{ 10 }, nano::block_hash{ 2 } });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first.account, nano::account{ 10 });
	ASSERT_EQ (found->first.hash, nano::block_hash{ 2 });

	// A miss parks at the first entry past the target, here the next group
	ASSERT_EQ (crawler.find (nano::pending_key{ nano::account{ 10 }, nano::block_hash{ 4 } }), nullptr);
	ASSERT_EQ (crawler.key ().account, nano::account{ 20 });
	ASSERT_EQ (crawler.key ().hash, nano::block_hash{ 1 });
}

TEST (crawler, find_group_compound)
{
	auto store = nano::test::make_store ();

	populate_pending (*store, {
							  { 10, { 1, 2, 3 } },
							  { 20, { 1 } },
							  { 30, { 1 } },
							  });

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	// A group hit returns the first entry of the group
	auto const * found = crawler.find_group (nano::account{ 20 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first.account, nano::account{ 20 });
	ASSERT_EQ (found->first.hash, nano::block_hash{ 1 });

	// A miss parks at the next group
	ASSERT_EQ (crawler.find_group (nano::account{ 25 }), nullptr);
	ASSERT_EQ (crawler.group_key (), nano::account{ 30 });
}

TEST (crawler, find_group_simple)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20 });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// For simple keys a group probe is equivalent to an entry probe
	auto const * found = crawler.find_group (nano::account{ 20 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ 20 });
}

TEST (crawler, find_within_sequential_window)
{
	auto store = nano::test::make_store ();

	// Entries equal to sequential_threshold
	std::vector<nano::uint256_t> keys;
	for (size_t i = 1; i <= sequential_threshold; ++i)
	{
		keys.push_back (i);
	}
	populate_accounts (*store, keys);

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 1 });

	// find() should reach the target via sequential iteration (within threshold)
	auto const * found = crawler.find (nano::account{ sequential_threshold / 2 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ sequential_threshold / 2 });
}

TEST (crawler, find_seek_fallback)
{
	auto store = nano::test::make_store ();

	// More elements than threshold between start and target forces seek fallback
	std::vector<nano::uint256_t> keys;
	for (size_t i = 1; i <= sequential_threshold + 2; ++i)
	{
		keys.push_back (i);
	}
	keys.push_back (100); // Target far from sequential range
	populate_accounts (*store, keys);

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 1 });

	// find(100) needs to traverse more than threshold elements, triggering seek fallback
	auto const * found = crawler.find (nano::account{ 100 });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ 100 });
}

TEST (crawler, find_target_at_window_edge)
{
	auto store = nano::test::make_store ();

	// Dense consecutive keys so the target sits exactly one entry past the sequential window
	std::vector<nano::uint256_t> keys;
	for (size_t i = 1; i <= sequential_threshold + 2; ++i)
	{
		keys.push_back (i);
	}
	populate_accounts (*store, keys);

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ 1 });

	// The window checks entries 1..threshold, the target is found by the fallback seek
	auto const * found = crawler.find (nano::account{ 1 + sequential_threshold });
	ASSERT_NE (found, nullptr);
	ASSERT_EQ (found->first, nano::account{ 1 + sequential_threshold });
}

TEST (crawler, find_large_scale)
{
	auto store = nano::test::make_store ();

	std::vector<nano::account> sorted_accounts;
	{
		auto txn = store->tx_begin_write ();
		for (int i = 0; i < 1000; ++i)
		{
			nano::account account;
			nano::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
			sorted_accounts.push_back (account);
			store->account.put (txn, account, nano::account_info{});
		}
	}
	std::sort (sorted_accounts.begin (), sorted_accounts.end (), [] (auto const & a, auto const & b) {
		return a.number () < b.number ();
	});

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	// Probe ascending positions, mixing short hops and long jumps
	for (size_t i : { 100, 250, 500, 750, 999 })
	{
		auto const * found = crawler.find (sorted_accounts[i]);
		ASSERT_NE (found, nullptr);
		ASSERT_EQ (found->first, sorted_accounts[i]);
	}
}

/*
 * Saturation & boundaries
 */

TEST (crawler, key_saturation_max_value)
{
	auto store = nano::test::make_store ();

	// Use max value and max-1
	auto max_val = std::numeric_limits<nano::uint256_t>::max ();
	auto max_minus_one = max_val - 1;

	populate_accounts (*store, { max_minus_one, max_val });

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn, nano::account{ max_minus_one });

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key ().number (), max_minus_one);

	// Should be able to advance to max
	ASSERT_TRUE (crawler.next_entry ());
	ASSERT_EQ (crawler.key ().number (), max_val);

	// Attempting to go past max ends iteration
	ASSERT_FALSE (crawler.next_entry ());
	ASSERT_FALSE (crawler);
}

TEST (crawler, boundary_first_key)
{
	auto store = nano::test::make_store ();

	populate_accounts (*store, { 10, 20, 30 });

	auto txn = store->tx_begin_read ();

	// Construct with start=0 should position at first key
	auto crawler1 = store->account.crawl (txn, nano::account{ 0 });
	ASSERT_EQ (crawler1.key (), nano::account{ 10 });

	// Construct with start=first key should position at first key
	auto crawler2 = store->account.crawl (txn, nano::account{ 10 });
	ASSERT_EQ (crawler2.key (), nano::account{ 10 });
}

/*
 * Large scale iteration
 */

TEST (crawler, large_scale_iteration)
{
	auto store = nano::test::make_store ();

	std::unordered_set<nano::account> accounts;
	{
		auto txn = store->tx_begin_write ();
		for (int i = 0; i < 1000; ++i)
		{
			nano::account account;
			nano::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
			accounts.insert (account);
			store->account.put (txn, account, nano::account_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto crawler = store->account.crawl (txn);

	std::unordered_set<nano::account> visited;
	nano::account previous{};
	for (; crawler; ++crawler)
	{
		auto current = crawler.key ();
		ASSERT_GT (current.number (), previous.number ());
		visited.insert (current);
		previous = current;
	}

	ASSERT_EQ (visited.size (), accounts.size ());
	ASSERT_EQ (visited, accounts);
}

TEST (crawler, large_scale_groups)
{
	auto store = nano::test::make_store ();

	// 100 accounts, each with 10 pending entries = 1000 total entries
	std::vector<std::pair<nano::uint256_t, std::vector<nano::uint256_t>>> entries;
	for (nano::uint256_t acc = 1; acc <= 100; ++acc)
	{
		std::vector<nano::uint256_t> hashes;
		for (nano::uint256_t h = 1; h <= 10; ++h)
		{
			hashes.push_back (h);
		}
		entries.push_back ({ acc, hashes });
	}
	populate_pending (*store, entries);

	auto txn = store->tx_begin_read ();
	auto crawler = store->pending.crawl (txn);

	size_t group_count = 0;
	nano::uint256_t previous = 0;
	for (; crawler; crawler.next_group ())
	{
		auto current = crawler.group_key ().number ();
		ASSERT_GT (current, previous);
		previous = current;
		++group_count;
	}

	// Should have visited exactly 100 groups
	ASSERT_EQ (group_count, 100);
}

/*
 * Refresh - transaction refresh with iterator re-establishment
 */

TEST (crawler, refresh_keeps_position)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (nano::uint256_t i = 1; i <= 5; ++i)
	{
		store->account.put (txn, nano::account{ i }, nano::account_info{});
	}

	auto crawler = store->account.crawl (txn);
	++crawler;
	++crawler;
	ASSERT_EQ (crawler.key (), nano::account{ 3 });

	crawler.refresh ();
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 3 });
}

TEST (crawler, refresh_during_iteration)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (nano::uint256_t i = 1; i <= 10; ++i)
	{
		store->account.put (txn, nano::account{ i }, nano::account_info{});
	}

	auto crawler = store->account.crawl (txn);

	// Iterate with periodic refresh (simulates batch processing)
	std::vector<nano::uint256_t> visited;
	size_t count = 0;
	while (crawler)
	{
		visited.push_back (crawler.key ().number ());
		++crawler;
		++count;
		if (count % 3 == 0)
		{
			crawler.refresh ();
		}
	}
	ASSERT_EQ (visited, (std::vector<nano::uint256_t>{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }));
}

TEST (crawler, refresh_at_end)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	store->account.put (txn, nano::account{ 1 }, nano::account_info{});

	auto crawler = store->account.crawl (txn);
	++crawler;
	ASSERT_FALSE (crawler);

	crawler.refresh ();
	ASSERT_FALSE (crawler);
}

TEST (crawler, refresh_after_delete)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (nano::uint256_t i = 1; i <= 5; ++i)
	{
		store->account.put (txn, nano::account{ i }, nano::account_info{});
	}

	auto crawler = store->account.crawl (txn, nano::account{ 3 });
	ASSERT_EQ (crawler.key (), nano::account{ 3 });

	// Refresh when current entry was deleted lands on next valid entry
	store->account.del (txn, nano::account{ 3 });
	crawler.refresh ();
	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 4 });
}

TEST (crawler, refresh_after_delete_last)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (nano::uint256_t i = 1; i <= 3; ++i)
	{
		store->account.put (txn, nano::account{ i }, nano::account_info{});
	}

	auto crawler = store->account.crawl (txn, nano::account{ 3 });
	ASSERT_EQ (crawler.key (), nano::account{ 3 });

	// Refresh when current entry was the last one and deleted → end
	store->account.del (txn, nano::account{ 3 });
	crawler.refresh ();
	ASSERT_FALSE (crawler);
}

TEST (crawler, refresh_if_needed)
{
	auto store = nano::test::make_store ();

	auto txn = store->tx_begin_write ();
	for (nano::uint256_t i = 1; i <= 3; ++i)
	{
		store->account.put (txn, nano::account{ i }, nano::account_info{});
	}

	auto crawler = store->account.crawl (txn);

	// Zero max age forces a refresh, position is preserved
	ASSERT_TRUE (crawler.refresh_if_needed (std::chrono::milliseconds{ 0 }));
	ASSERT_EQ (crawler.key (), nano::account{ 1 });

	// Freshly refreshed transaction should not refresh again
	ASSERT_FALSE (crawler.refresh_if_needed ());
	ASSERT_EQ (crawler.key (), nano::account{ 1 });
}

/*
 * Misc
 */

TEST (crawler, const_transaction)
{
	auto store = nano::test::make_store ();

	{
		auto txn = store->tx_begin_write ();
		for (nano::uint256_t i = 1; i <= 5; ++i)
		{
			store->account.put (txn, nano::account{ i }, nano::account_info{});
		}
	}

	auto txn = store->tx_begin_read ();
	auto const & const_txn = txn;
	auto crawler = store->account.crawl (const_txn);

	ASSERT_TRUE (crawler);
	ASSERT_EQ (crawler.key (), nano::account{ 1 });

	std::vector<nano::uint256_t> visited;
	for (; crawler; ++crawler)
	{
		visited.push_back (crawler.key ().number ());
	}
	ASSERT_EQ (visited, (std::vector<nano::uint256_t>{ 1, 2, 3, 4, 5 }));
}
