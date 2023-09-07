#include <nano/node/scheduler/limiter.hpp>
#include <nano/secure/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (scheduler_limiter, construction)
{
	auto occupancy = std::make_shared<nano::scheduler::limiter> (nano::test::active_transactions_insert_null, 1, nano::election_behavior::normal);
	ASSERT_EQ (1, occupancy->limit ());
	ASSERT_TRUE (occupancy->available ());
}

TEST (scheduler_limiter, limit)
{
	auto occupancy = std::make_shared<nano::scheduler::limiter> (nano::test::active_transactions_insert_null, 1, nano::election_behavior::normal);
	ASSERT_EQ (1, occupancy->limit ());
	ASSERT_TRUE (occupancy->available ());
}

TEST (scheduler_limiter, election_activate_observer)
{
	nano::test::system system{ 1 };
	auto occupancy = std::make_shared<nano::scheduler::limiter> ([&] (auto const & block, auto const & behavior) {
		return system.node (0).active.insert (block, behavior);
	},
	1, nano::election_behavior::normal);
	auto result = occupancy->activate (nano::dev::genesis);
	ASSERT_TRUE (result.inserted);
	auto elections = occupancy->elections ();
	ASSERT_EQ (1, elections.size ());
	ASSERT_EQ (1, elections.count (nano::dev::genesis->qualified_root ()));
	ASSERT_FALSE (occupancy->available ());
	result.election = nullptr; // Implicitly run election destructor notification by clearing the last reference
	ASSERT_TIMELY (5s, occupancy->available ());
}
