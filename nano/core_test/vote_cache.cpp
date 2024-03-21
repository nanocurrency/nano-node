#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <map>

namespace
{
std::map<nano::account, nano::uint128_t> & rep_to_weight_map ()
{
	static std::map<nano::account, nano::uint128_t> map;
	return map;
}

std::function<nano::uint128_t (nano::account const & rep)> rep_weight_query ()
{
	return [] (nano::account const & rep) { return rep_to_weight_map ()[rep]; };
}

void register_rep (nano::account & rep, nano::uint128_t weight)
{
	auto & map = rep_to_weight_map ();
	map[rep] = weight;
}

nano::keypair create_rep (nano::uint128_t weight)
{
	nano::keypair key;
	register_rep (key.pub, weight);
	return key;
}
}

TEST (vote_cache, construction)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	ASSERT_EQ (0, vote_cache.size ());
	ASSERT_TRUE (vote_cache.empty ());
	auto hash1 = nano::test::random_hash ();
	ASSERT_TRUE (vote_cache.find (hash1).empty ());
}

/*
 * Inserts single hash to cache, ensures it can be retrieved and dequeued
 */
TEST (vote_cache, insert_one_hash)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto rep1 = create_rep (7);
	auto hash1 = nano::test::random_hash ();
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
	vote_cache.insert (vote1);
	ASSERT_EQ (1, vote_cache.size ());

	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote1);

	auto tops = vote_cache.top (0);
	ASSERT_EQ (tops.size (), 1);
	ASSERT_EQ (tops[0].hash, hash1);
	ASSERT_EQ (tops[0].tally, 7);
	ASSERT_EQ (tops[0].final_tally, 0);
}

/*
 * Inserts multiple votes for single hash
 * Ensures all of them can be retrieved and that tally is properly accumulated
 */
TEST (vote_cache, insert_one_hash_many_votes)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep2, { hash1 }, 2 * 1024 * 1024);
	auto vote3 = nano::test::make_vote (rep3, { hash1 }, 3 * 1024 * 1024);
	vote_cache.insert (vote1);
	vote_cache.insert (vote2);
	vote_cache.insert (vote3);

	ASSERT_EQ (1, vote_cache.size ());
	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 3);
	// Verify each vote is present
	ASSERT_TRUE (std::find (peek1.begin (), peek1.end (), vote1) != peek1.end ());
	ASSERT_TRUE (std::find (peek1.begin (), peek1.end (), vote2) != peek1.end ());
	ASSERT_TRUE (std::find (peek1.begin (), peek1.end (), vote3) != peek1.end ());

	auto tops = vote_cache.top (0);
	ASSERT_EQ (tops.size (), 1);
	ASSERT_EQ (tops[0].hash, hash1);
	ASSERT_EQ (tops[0].tally, 7 + 9 + 11);
	ASSERT_EQ (tops[0].final_tally, 0);
}

/*
 * Inserts multiple votes for multiple hashes
 * Ensures all of them can be retrieved and that queue returns the highest tally entries first
 */
TEST (vote_cache, insert_many_hashes_many_votes)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	// There will be 3 random hashes to vote for
	auto hash1 = nano::test::random_hash ();
	auto hash2 = nano::test::random_hash ();
	auto hash3 = nano::test::random_hash ();
	// There will be 4 reps with different weights
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto rep4 = create_rep (13);
	// Votes: rep1 > hash1, rep2 > hash2, rep3 > hash3, rep4 > hash1 (the same as rep1)
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep2, { hash2 }, 1024 * 1024);
	auto vote3 = nano::test::make_vote (rep3, { hash3 }, 1024 * 1024);
	auto vote4 = nano::test::make_vote (rep4, { hash1 }, 1024 * 1024);
	// Insert first 3 votes in cache
	vote_cache.insert (vote1);
	vote_cache.insert (vote2);
	vote_cache.insert (vote3);
	// Ensure all of those are properly inserted
	ASSERT_EQ (3, vote_cache.size ());
	ASSERT_EQ (1, vote_cache.find (hash1).size ());
	ASSERT_EQ (1, vote_cache.find (hash2).size ());
	ASSERT_EQ (1, vote_cache.find (hash3).size ());

	// Ensure that first entry in queue is the one for hash3 (rep3 has the highest weight of the first 3 reps)
	auto tops1 = vote_cache.top (0);
	ASSERT_EQ (tops1.size (), 3);
	ASSERT_EQ (tops1[0].hash, hash3);
	ASSERT_EQ (tops1[0].tally, 11);

	auto peek1 = vote_cache.find (hash3);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote3);

	// Now add a vote from rep4 with the highest voting weight
	vote_cache.insert (vote4);

	// Ensure that the first entry in queue is now the one for hash1 (rep1 + rep4 tally weight)
	auto tops2 = vote_cache.top (0);
	ASSERT_EQ (tops1.size (), 3);
	ASSERT_EQ (tops2[0].hash, hash1);
	ASSERT_EQ (tops2[0].tally, 7 + 13);

	auto pop1 = vote_cache.find (hash1);
	ASSERT_EQ (pop1.size (), 2);
	ASSERT_TRUE (std::find (pop1.begin (), pop1.end (), vote1) != pop1.end ());
	ASSERT_TRUE (std::find (pop1.begin (), pop1.end (), vote4) != pop1.end ());

	// The next entry in queue should be hash3 (rep3 tally weight)
	ASSERT_EQ (tops2[1].hash, hash3);
	ASSERT_EQ (tops2[1].tally, 11);

	auto pop2 = vote_cache.find (hash3);
	ASSERT_EQ (pop2.size (), 1);
	ASSERT_EQ (pop2.front (), vote3);

	// And last one should be hash2 with rep2 tally weight
	ASSERT_EQ (tops2[2].hash, hash2);
	ASSERT_EQ (tops2[2].tally, 9);

	auto pop3 = vote_cache.find (hash2);
	ASSERT_EQ (pop3.size (), 1);
	ASSERT_EQ (pop3.front (), vote2);
}

/*
 * Ensure that duplicate votes are ignored
 */
TEST (vote_cache, insert_duplicate)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote1);
	vote_cache.insert (vote2);
	ASSERT_EQ (1, vote_cache.size ());
}

/*
 * Ensure that when processing vote from a representative that is already cached, we always update to the vote with the highest timestamp
 */
TEST (vote_cache, insert_newer)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote1);
	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote1);
	auto vote2 = nano::test::make_final_vote (rep1, { hash1 });
	vote_cache.insert (vote2);
	auto peek2 = vote_cache.find (hash1);
	ASSERT_EQ (peek2.size (), 1);
	ASSERT_EQ (peek2.front (), vote2); // vote2 should replace vote1 as it has a higher timestamp
}

/*
 * Ensure that when processing vote from a representative that is already cached, votes with older timestamp are ignored
 */
TEST (vote_cache, insert_older)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 2 * 1024 * 1024);
	vote_cache.insert (vote1);
	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote1);
	auto vote2 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote2);
	auto peek2 = vote_cache.find (hash1);
	ASSERT_EQ (peek2.size (), 1);
	ASSERT_EQ (peek2.front (), vote1); // vote1 should still be in cache as it has a higher timestamp
}

/*
 * Ensure that erase functionality works
 */
TEST (vote_cache, erase)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = nano::test::random_hash ();
	auto hash2 = nano::test::random_hash ();
	auto hash3 = nano::test::random_hash ();
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto rep4 = create_rep (13);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep2, { hash2 }, 1024 * 1024);
	auto vote3 = nano::test::make_vote (rep3, { hash3 }, 1024 * 1024);
	vote_cache.insert (vote1);
	vote_cache.insert (vote2);
	vote_cache.insert (vote3);
	ASSERT_EQ (3, vote_cache.size ());
	ASSERT_FALSE (vote_cache.empty ());
	ASSERT_FALSE (vote_cache.find (hash1).empty ());
	ASSERT_FALSE (vote_cache.find (hash2).empty ());
	ASSERT_FALSE (vote_cache.find (hash3).empty ());
	vote_cache.erase (hash2);
	ASSERT_EQ (2, vote_cache.size ());
	ASSERT_FALSE (vote_cache.find (hash1).empty ());
	ASSERT_TRUE (vote_cache.find (hash2).empty ());
	ASSERT_FALSE (vote_cache.find (hash3).empty ());
	vote_cache.erase (hash1);
	vote_cache.erase (hash3);
	ASSERT_TRUE (vote_cache.find (hash1).empty ());
	ASSERT_TRUE (vote_cache.find (hash2).empty ());
	ASSERT_TRUE (vote_cache.find (hash3).empty ());
	ASSERT_TRUE (vote_cache.empty ());
}

/*
 * Ensure that when cache is overfilled, we remove the oldest entries first
 */
TEST (vote_cache, overfill)
{
	nano::test::system system;
	// Create a vote cache with max size set to 1024
	nano::vote_cache_config cfg;
	cfg.max_size = 1024;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	const int count = 16 * 1024;
	for (int n = 0; n < count; ++n)
	{
		// The more recent the vote, the less voting weight it has
		auto rep1 = create_rep (count - n);
		auto hash1 = nano::test::random_hash ();
		auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
		vote_cache.insert (vote1);
	}
	ASSERT_LT (vote_cache.size (), count);
	// Check that oldest votes are dropped first
	auto tops = vote_cache.top (0);
	ASSERT_EQ (tops.size (), 1024);
	ASSERT_EQ (tops[0].tally, 1024);
}

/*
 * Check that when a single vote cache entry is overfilled, it ignores any new votes
 */
TEST (vote_cache, overfill_entry)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();
	const int count = 1024;
	auto hash1 = nano::test::random_hash ();
	for (int n = 0; n < count; ++n)
	{
		auto rep1 = create_rep (9);
		auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
		vote_cache.insert (vote1);
	}
	ASSERT_EQ (1, vote_cache.size ());
}

TEST (vote_cache, age_cutoff)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	cfg.age_cutoff = std::chrono::seconds{ 3 };
	nano::vote_cache vote_cache{ cfg, system.stats };
	vote_cache.rep_weight_query = rep_weight_query ();

	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 3);
	vote_cache.insert (vote1);
	ASSERT_EQ (1, vote_cache.size ());
	ASSERT_FALSE (vote_cache.find (hash1).empty ());

	auto tops1 = vote_cache.top (0);
	ASSERT_EQ (tops1.size (), 1);
	ASSERT_EQ (tops1[0].hash, hash1);
	ASSERT_EQ (system.stats.count (nano::stat::type::vote_cache, nano::stat::detail::cleanup), 0);

	// Wait for first cleanup
	auto check = [&] () {
		// Cleanup is performed periodically when calling `top ()`
		vote_cache.top (0);
		return system.stats.count (nano::stat::type::vote_cache, nano::stat::detail::cleanup);
	};
	ASSERT_TIMELY_EQ (5s, 1, check ());

	// After first cleanup the entry should still be there
	auto tops2 = vote_cache.top (0);
	ASSERT_EQ (tops2.size (), 1);

	// After 3 seconds the entry should be removed
	ASSERT_TIMELY (5s, vote_cache.top (0).empty ());
}