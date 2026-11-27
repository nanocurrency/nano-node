#include <nano/crypto_lib/random_pool.hpp>
#include <nano/lib/stats.hpp>
#include <nano/node/bootstrap/account_sets_index.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace
{
nano::block_hash random_hash ()
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}
}

/*
 * account_sets
 */

TEST (account_sets, construction)
{
	nano::test::system system;
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
}

TEST (account_sets, empty_blocked)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, block)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_up (account);
	sets.block (account, random_hash ());
	ASSERT_TRUE (sets.blocked (account));
}

TEST (account_sets, unblock)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	auto hash = random_hash ();
	sets.priority_up (account);
	sets.block (account, hash);
	ASSERT_TRUE (sets.blocked (account));
	sets.unblock (account, hash);
	ASSERT_FALSE (sets.blocked (account));
}

TEST (account_sets, priority_base)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_blocked)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.block (account, random_hash ());
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_unblock)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_initial);
	auto hash = random_hash ();
	sets.block (account, hash);
	ASSERT_EQ (0.0, sets.priority (account));
	sets.unblock (account, hash);
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_initial);
}

TEST (account_sets, priority_up_down)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_initial);
	sets.priority_down (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_initial / nano::bootstrap::account_sets_index::priority_divide);
}

TEST (account_sets, priority_down_empty)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_down (account);
	ASSERT_EQ (0.0, sets.priority (account));
}

TEST (account_sets, priority_down_saturate)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_up (account);
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_initial);
	for (int n = 0; n < 1000; ++n)
	{
		sets.priority_down (account);
	}
	ASSERT_FALSE (sets.prioritized (account));
}

TEST (account_sets, priority_set)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	sets.priority_set (account, 10.0);
	ASSERT_EQ (sets.priority (account), 10.0);
}

// Ensure priority value is bounded
TEST (account_sets, saturate_priority)
{
	nano::test::system system;

	nano::account account{ 1 };
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };
	for (int n = 0; n < 1000; ++n)
	{
		sets.priority_up (account);
	}
	ASSERT_EQ (sets.priority (account), nano::bootstrap::account_sets_index::priority_max);
}

TEST (account_sets, decay_blocking)
{
	using namespace std::chrono_literals;

	nano::test::system system;
	nano::account_sets_config config;
	config.blocking_decay = 1s;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	// Test empty set
	ASSERT_EQ (0, sets.decay_blocking ());

	// Create test accounts and timestamps
	nano::account account1{ 1 };
	nano::account account2{ 2 };
	nano::account account3{ 3 };

	auto now = std::chrono::steady_clock::now ();

	// Add first account
	sets.priority_up (account1);
	sets.block (account1, random_hash (), now);
	ASSERT_TRUE (sets.blocked (account1));
	ASSERT_EQ (1, sets.blocked_size ());

	// Decay before timeout should not remove entry
	ASSERT_EQ (0, sets.decay_blocking (now));
	ASSERT_TRUE (sets.blocked (account1));
	ASSERT_EQ (1, sets.blocked_size ());

	// Add second account after 500ms
	now += 500ms;
	sets.priority_up (account2);
	sets.block (account2, random_hash (), now);
	ASSERT_TRUE (sets.blocked (account2));
	ASSERT_EQ (2, sets.blocked_size ());

	// Add third account after another 500ms
	now += 500ms;
	sets.priority_up (account3);
	sets.block (account3, random_hash (), now);
	ASSERT_TRUE (sets.blocked (account3));
	ASSERT_EQ (3, sets.blocked_size ());

	// Decay at 1.5s - should remove first two accounts
	now += 500ms;
	ASSERT_EQ (2, sets.decay_blocking (now));
	ASSERT_FALSE (sets.blocked (account1));
	ASSERT_FALSE (sets.blocked (account2));
	ASSERT_TRUE (sets.blocked (account3));
	ASSERT_EQ (1, sets.blocked_size ());

	// Reinsert second account
	auto hash2 = random_hash ();
	sets.priority_up (account2);
	sets.block (account2, hash2, now);
	ASSERT_TRUE (sets.blocked (account2));
	ASSERT_EQ (2, sets.blocked_size ());

	// Immediate decay should not affect reinserted account
	ASSERT_EQ (0, sets.decay_blocking (now));
	ASSERT_TRUE (sets.blocked (account2));

	// Decay at 2s - should remove account3 but keep reinserted account2
	now += 500ms;
	ASSERT_EQ (1, sets.decay_blocking (now));
	ASSERT_FALSE (sets.blocked (account3));
	ASSERT_TRUE (sets.blocked (account2));
	ASSERT_EQ (1, sets.blocked_size ());

	// Final decay after another second - should remove remaining account
	now += 1s;
	ASSERT_EQ (1, sets.decay_blocking (now));
	ASSERT_FALSE (sets.blocked (account2));
	ASSERT_EQ (0, sets.blocked_size ());
}

/*
 * The account of a blocked account's dependency is asked for once, then again only after a cooldown that doubles with every request.
 */
TEST (account_sets, dependency_request_cooldown)
{
	nano::test::system system;
	nano::account_sets_config config;
	config.dependency_cooldown = 1s;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto const now = std::chrono::steady_clock::now ();

	// A freshly blocked account has its dependency due right away
	nano::account account{ 1 };
	auto const dependency = random_hash ();
	sets.priority_up (account);
	sets.block (account, dependency, now);
	ASSERT_EQ (sets.next_blocking (any, now), dependency);

	// Once asked for, it waits out the cooldown
	sets.dependency_requested (dependency, now);
	ASSERT_TRUE (sets.next_blocking (any, now).is_zero ());
	ASSERT_TRUE (sets.next_blocking (any, now + 1s - 1ms).is_zero ());
	ASSERT_EQ (sets.next_blocking (any, now + 1s), dependency);

	// A second request doubles the wait
	sets.dependency_requested (dependency, now + 1s);
	ASSERT_TRUE (sets.next_blocking (any, now + 1s + 2s - 1ms).is_zero ());
	ASSERT_EQ (sets.next_blocking (any, now + 1s + 2s), dependency);

	// And a third doubles it again
	sets.dependency_requested (dependency, now + 3s);
	ASSERT_TRUE (sets.next_blocking (any, now + 3s + 4s - 1ms).is_zero ());
	ASSERT_EQ (sets.next_blocking (any, now + 3s + 4s), dependency);
}

/*
 * The cooldown stops growing once it is as long as a blocked account stays in the set.
 */
TEST (account_sets, dependency_request_cooldown_limit)
{
	nano::test::system system;
	nano::account_sets_config config;
	config.dependency_cooldown = 1s;
	config.blocking_decay = 4s;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto now = std::chrono::steady_clock::now ();

	nano::account account{ 1 };
	auto const dependency = random_hash ();
	sets.priority_up (account);
	sets.block (account, dependency, now);

	// The first three requests wait 1, 2 and 4 seconds
	for (auto const wait : { 1s, 2s, 4s })
	{
		sets.dependency_requested (dependency, now);
		ASSERT_TRUE (sets.next_blocking (any, now + wait - 1ms).is_zero ());
		ASSERT_EQ (sets.next_blocking (any, now + wait), dependency);
		now += wait;
	}

	// Every later one waits 4 seconds as well
	for (int n = 0; n < 5; ++n)
	{
		sets.dependency_requested (dependency, now);
		ASSERT_TRUE (sets.next_blocking (any, now + 4s - 1ms).is_zero ());
		ASSERT_EQ (sets.next_blocking (any, now + 4s), dependency);
		now += 4s;
	}
}

/*
 * Without a cooldown a dependency is due again as soon as it was asked for, however often that happened.
 */
TEST (account_sets, dependency_request_no_cooldown)
{
	nano::test::system system;
	nano::account_sets_config config;
	config.dependency_cooldown = 0ms;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto const now = std::chrono::steady_clock::now ();

	nano::account account{ 1 };
	auto const dependency = random_hash ();
	sets.priority_up (account);
	sets.block (account, dependency, now);

	for (int n = 0; n < 5; ++n)
	{
		sets.dependency_requested (dependency, now);
		ASSERT_EQ (sets.next_blocking (any, now), dependency);
	}
}

/*
 * Dependencies are offered in the order they became due, so one that keeps coming back empty does not hold up the ones behind it.
 */
TEST (account_sets, next_blocking_due_order)
{
	nano::test::system system;
	nano::account_sets_config config;
	config.dependency_cooldown = 1s;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto const now = std::chrono::steady_clock::now ();

	// Nothing is offered while no account is blocked
	ASSERT_TRUE (sets.next_blocking (any, now).is_zero ());

	// Two blocked accounts, the first one blocked earlier
	nano::account account1{ 1 };
	nano::account account2{ 2 };
	auto const dependency1 = random_hash ();
	auto const dependency2 = random_hash ();
	sets.priority_up (account1);
	sets.block (account1, dependency1, now);
	sets.priority_up (account2);
	sets.block (account2, dependency2, now);

	// The first one is offered first, and once asked for the second one is next
	ASSERT_EQ (sets.next_blocking (any, now), dependency1);
	sets.dependency_requested (dependency1, now);
	ASSERT_EQ (sets.next_blocking (any, now), dependency2);

	// The second one is asked for later, so the first one comes off its cooldown ahead of it
	sets.dependency_requested (dependency2, now + 500ms);
	ASSERT_TRUE (sets.next_blocking (any, now + 500ms).is_zero ());
	ASSERT_EQ (sets.next_blocking (any, now + 1s), dependency1);

	// A dependency that is due but skipped by the filter does not hide the one behind it
	auto const skip_first = [&dependency1] (nano::block_hash const & hash) { return hash != dependency1; };
	ASSERT_TRUE (sets.next_blocking (skip_first, now + 1s).is_zero ());
	ASSERT_EQ (sets.next_blocking (skip_first, now + 1500ms), dependency2);

	// An account blocked meanwhile was never asked for, so it is offered ahead of both
	nano::account account3{ 3 };
	auto const dependency3 = random_hash ();
	sets.priority_up (account3);
	sets.block (account3, dependency3, now + 1500ms);
	ASSERT_EQ (sets.next_blocking (any, now + 1500ms), dependency3);

	// With all three due and every one of them skipped, nothing is offered
	auto const none = [] (nano::block_hash const &) { return false; };
	ASSERT_TRUE (sets.next_blocking (none, now + 1500ms).is_zero ());
}

/*
 * Accounts blocked on the same dependency go on cooldown together, since one request serves them all.
 */
TEST (account_sets, dependency_request_shared)
{
	nano::test::system system;
	nano::account_sets_config config;
	config.dependency_cooldown = 1s;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto const now = std::chrono::steady_clock::now ();

	// Two accounts wait for the same block
	nano::account account1{ 1 };
	nano::account account2{ 2 };
	auto const dependency = random_hash ();
	sets.priority_up (account1);
	sets.block (account1, dependency, now);
	sets.priority_up (account2);
	sets.block (account2, dependency, now);

	// One request puts both entries on cooldown
	ASSERT_EQ (sets.next_blocking (any, now), dependency);
	sets.dependency_requested (dependency, now);
	ASSERT_TRUE (sets.next_blocking (any, now).is_zero ());
	ASSERT_EQ (sets.next_blocking (any, now + 1s), dependency);

	// Recording a request for a hash nothing waits on changes nothing
	sets.dependency_requested (random_hash (), now);
	ASSERT_EQ (sets.next_blocking (any, now + 1s), dependency);
}

/*
 * Once the account of a dependency is known it is no longer asked for, and syncing puts that account into the priority set.
 */
TEST (account_sets, dependency_update_and_sync)
{
	nano::test::system system;
	nano::account_sets_config config;
	nano::bootstrap::account_sets_index sets{ config, system.stats };

	auto const any = [] (nano::block_hash const &) { return true; };
	auto const now = std::chrono::steady_clock::now ();

	// Two blocked accounts with unknown dependency accounts
	nano::account account1{ 1 };
	nano::account account2{ 2 };
	nano::account dependency_account{ 100 };
	auto const dependency1 = random_hash ();
	auto const dependency2 = random_hash ();
	sets.priority_up (account1);
	sets.block (account1, dependency1, now);
	sets.priority_up (account2);
	sets.block (account2, dependency2, now);

	// Nothing to sync while no dependency account is known
	ASSERT_EQ (sets.sync_dependencies (), 0);
	ASSERT_FALSE (sets.prioritized (dependency_account));

	// The first dependency gets its account, which leaves only the second one to ask for
	sets.dependency_update (dependency1, dependency_account);
	ASSERT_EQ (sets.next_blocking (any, now), dependency2);
	sets.dependency_requested (dependency2, now);
	ASSERT_TRUE (sets.next_blocking (any, now).is_zero ());

	// Syncing prioritizes the known dependency account, once
	ASSERT_EQ (sets.sync_dependencies (), 1);
	ASSERT_TRUE (sets.prioritized (dependency_account));
	ASSERT_EQ (sets.sync_dependencies (), 0);
}
