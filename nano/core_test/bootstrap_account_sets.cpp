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
