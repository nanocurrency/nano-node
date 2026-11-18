#include <nano/lib/keypair.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

/*
 * Results hold one slot per hash of the vote, so recording one past the last hash stops the node instead of writing outside them.
 */
TEST (vote_results_DeathTest, set_outside_vote)
{
	testing::FLAGS_gtest_death_test_style = "threadsafe";

	// A vote for two hashes
	nano::keypair key;
	auto const vote = nano::test::make_vote (key, { nano::test::random_hash (), nano::test::random_hash () }, 1);
	nano::vote_results results{ vote };

	// The last hash takes a result
	results.set (1, nano::vote_code::vote);
	ASSERT_EQ (1, results.size ());
	ASSERT_EQ (nano::vote_code::vote, results.at (vote->hashes[1]));

	// The position after it belongs to no hash
	ASSERT_DEATH_IF_SUPPORTED (results.set (2, nano::vote_code::vote), "position < codes.size");
}
