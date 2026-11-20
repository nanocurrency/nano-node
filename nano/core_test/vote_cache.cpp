#include <nano/node/election.hpp>
#include <nano/node/vote_cache.hpp>
#include <nano/node/vote_router.hpp>
#include <nano/secure/vote.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace
{
std::map<nano::account, nano::uint128_t> & rep_to_weight_map ()
{
	static std::map<nano::account, nano::uint128_t> map;
	return map;
}

void register_rep (nano::account const & rep, nano::uint128_t weight)
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

// Weight registered for a representative
nano::uint128_t rep_weight (nano::account const & rep)
{
	return rep_to_weight_map ()[rep];
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
	auto rep1 = create_rep (7);
	auto hash1 = nano::test::random_hash ();
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
	ASSERT_FALSE (vote_cache.contains (hash1));
	vote_cache.insert (vote1, rep_weight (vote1->account));
	ASSERT_TRUE (vote_cache.contains (hash1));
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
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep2, { hash1 }, 2 * 1024 * 1024);
	auto vote3 = nano::test::make_vote (rep3, { hash1 }, 3 * 1024 * 1024);
	vote_cache.insert (vote1, rep_weight (vote1->account));
	vote_cache.insert (vote2, rep_weight (vote2->account));
	vote_cache.insert (vote3, rep_weight (vote3->account));

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
	vote_cache.insert (vote1, rep_weight (vote1->account));
	vote_cache.insert (vote2, rep_weight (vote2->account));
	vote_cache.insert (vote3, rep_weight (vote3->account));
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
	vote_cache.insert (vote4, rep_weight (vote4->account));

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
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	auto vote2 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote1, 0 /* any weight */);
	vote_cache.insert (vote2, 0 /* any weight */);
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
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote1, 0 /* any weight */);
	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote1);
	auto vote2 = nano::test::make_final_vote (rep1, { hash1 });
	vote_cache.insert (vote2, 0 /* any weight */);
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
	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 2 * 1024 * 1024);
	vote_cache.insert (vote1, 0 /* any weight */);
	auto peek1 = vote_cache.find (hash1);
	ASSERT_EQ (peek1.size (), 1);
	ASSERT_EQ (peek1.front (), vote1);
	auto vote2 = nano::test::make_vote (rep1, { hash1 }, 1 * 1024 * 1024);
	vote_cache.insert (vote2, 0 /* any weight */);
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
	ASSERT_TRUE (vote_cache.empty ());
	ASSERT_FALSE (vote_cache.contains (hash1));
	vote_cache.insert (vote1, 0 /* any weight */);
	vote_cache.insert (vote2, 0 /* any weight */);
	vote_cache.insert (vote3, 0 /* any weight */);
	ASSERT_TRUE (vote_cache.contains (hash1));
	ASSERT_TRUE (vote_cache.contains (hash2));
	ASSERT_TRUE (vote_cache.contains (hash3));
	ASSERT_EQ (3, vote_cache.size ());
	ASSERT_FALSE (vote_cache.empty ());
	ASSERT_FALSE (vote_cache.find (hash1).empty ());
	ASSERT_FALSE (vote_cache.find (hash2).empty ());
	ASSERT_FALSE (vote_cache.find (hash3).empty ());
	vote_cache.erase (hash2);
	ASSERT_EQ (2, vote_cache.size ());
	ASSERT_FALSE (vote_cache.contains (hash2));
	ASSERT_FALSE (vote_cache.find (hash1).empty ());
	ASSERT_TRUE (vote_cache.find (hash2).empty ());
	ASSERT_FALSE (vote_cache.find (hash3).empty ());
	vote_cache.erase (hash1);
	vote_cache.erase (hash3);
	ASSERT_EQ (0, vote_cache.size ());
	ASSERT_FALSE (vote_cache.contains (hash1));
	ASSERT_FALSE (vote_cache.contains (hash2));
	ASSERT_FALSE (vote_cache.contains (hash3));
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
	const int count = 16 * 1024;
	for (int n = 0; n < count; ++n)
	{
		// The more recent the vote, the less voting weight it has
		auto rep1 = create_rep (count - n);
		auto hash1 = nano::test::random_hash ();
		auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
		vote_cache.insert (vote1, rep_weight (vote1->account));
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
	const int count = 1024;
	auto hash1 = nano::test::random_hash ();
	for (int n = 0; n < count; ++n)
	{
		auto rep1 = create_rep (9);
		auto vote1 = nano::test::make_vote (rep1, { hash1 }, 1024 * 1024);
		vote_cache.insert (vote1, 0 /* any weight */);
	}
	ASSERT_EQ (1, vote_cache.size ());
}

/*
 * A full entry keeps its heaviest voters: every further voter pushes out the lightest one, which may be that voter itself.
 * Reaching the limit already pushes one out, so an entry holds one voter less than the limit.
 */
TEST (vote_cache, entry_keeps_heaviest_voters)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	cfg.max_voters = 4;
	nano::vote_cache vote_cache{ cfg, system.stats };
	auto const hash = nano::test::random_hash ();

	auto voters = [&] () {
		std::set<nano::account> result;
		for (auto const & vote : vote_cache.find (hash))
		{
			result.insert (vote->account);
		}
		return result;
	};

	auto const rep10 = create_rep (10);
	auto const rep30 = create_rep (30);
	auto const rep20 = create_rep (20);
	auto const rep40 = create_rep (40);
	for (auto const & rep : { rep10, rep30, rep20, rep40 })
	{
		vote_cache.insert (nano::test::make_vote (rep, { hash }, 1), rep_weight (rep.pub));
	}
	ASSERT_EQ ((std::set<nano::account>{ rep30.pub, rep20.pub, rep40.pub }), voters ());

	// A voter lighter than all of them does not stay
	auto const rep5 = create_rep (5);
	vote_cache.insert (nano::test::make_vote (rep5, { hash }, 1), rep_weight (rep5.pub));
	ASSERT_EQ ((std::set<nano::account>{ rep30.pub, rep20.pub, rep40.pub }), voters ());

	// A heavier voter takes the place of the lightest one
	auto const rep25 = create_rep (25);
	vote_cache.insert (nano::test::make_vote (rep25, { hash }, 1), rep_weight (rep25.pub));
	ASSERT_EQ ((std::set<nano::account>{ rep30.pub, rep40.pub, rep25.pub }), voters ());

	// Among equally light voters the earliest one goes
	auto const rep25_later = create_rep (25);
	vote_cache.insert (nano::test::make_vote (rep25_later, { hash }, 1), rep_weight (rep25_later.pub));
	ASSERT_EQ ((std::set<nano::account>{ rep30.pub, rep40.pub, rep25_later.pub }), voters ());

	// The tally follows the voters that stayed
	auto const top = vote_cache.top (0);
	ASSERT_EQ (1, top.size ());
	ASSERT_EQ (30 + 40 + 25, top[0].tally);
}

/*
 * A newer vote of a known voter replaces the old one, the tally only changes when the vote becomes final.
 */
TEST (vote_cache, entry_newer_vote_replaces_older)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };
	auto const hash = nano::test::random_hash ();
	auto const rep = create_rep (7);

	vote_cache.insert (nano::test::make_vote (rep, { hash }, 1024 * 1024), rep_weight (rep.pub));
	auto const newer = nano::test::make_vote (rep, { hash }, 2 * 1024 * 1024);
	vote_cache.insert (newer, rep_weight (newer->account));
	ASSERT_EQ (1, vote_cache.find (hash).size ());
	ASSERT_EQ (newer, vote_cache.find (hash)[0]);
	ASSERT_EQ (7, vote_cache.top (0)[0].tally);
	ASSERT_EQ (0, vote_cache.top (0)[0].final_tally);

	auto const final_vote = nano::test::make_final_vote (rep, { hash });
	vote_cache.insert (final_vote, rep_weight (final_vote->account));
	ASSERT_EQ (final_vote, vote_cache.find (hash)[0]);
	ASSERT_EQ (7, vote_cache.top (0)[0].tally);
	ASSERT_EQ (7, vote_cache.top (0)[0].final_tally);
}

TEST (vote_cache, age_cutoff)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	cfg.age_cutoff = std::chrono::seconds{ 3 };
	nano::vote_cache vote_cache{ cfg, system.stats };

	auto hash1 = nano::test::random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = nano::test::make_vote (rep1, { hash1 }, 3);
	vote_cache.insert (vote1, 0 /* any weight */);
	ASSERT_EQ (1, vote_cache.size ());
	ASSERT_FALSE (vote_cache.find (hash1).empty ());

	auto tops1 = vote_cache.top (0);
	ASSERT_EQ (tops1.size (), 1);
	ASSERT_EQ (tops1[0].hash, hash1);

	system.stats.clear ();

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
namespace
{
// Stores a routed vote for which every hash got the same routing result
void insert_routed (nano::vote_cache & vote_cache, std::shared_ptr<nano::vote> const & vote, nano::vote_code code)
{
	nano::vote_results results{ vote };
	for (size_t position = 0; position < vote->hashes.size (); ++position)
	{
		results.set (position, code);
	}
	vote_cache.insert (vote, rep_weight (vote->account), results);
}
}

/*
 * A block that no election claims is reported exactly once, by the vote that brings its cached tally up to the threshold.
 * Three representatives of weight 60 vote for one hash against a threshold of 100: the first vote stays below it,
 * the second reaches it and the third finds it there already.
 */
TEST (vote_cache, threshold_reached_once)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };

	// Collect the reports, against a fixed threshold
	std::deque<nano::block_hash> reported;
	vote_cache.threshold_reached.add ([&reported] (nano::block_hash const & hash) {
		reported.push_back (hash);
	});
	vote_cache.threshold_query = [] () {
		return nano::uint128_t{ 100 };
	};

	auto const hash = nano::test::random_hash ();
	auto const rep1 = create_rep (60);
	auto const rep2 = create_rep (60);
	auto const rep3 = create_rep (60);

	// A tally of 60 is below the threshold
	insert_routed (vote_cache, nano::test::make_vote (rep1, { hash }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_TRUE (reported.empty ());

	// A tally of 120 reaches it
	insert_routed (vote_cache, nano::test::make_vote (rep2, { hash }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_EQ (reported.size (), 1);
	ASSERT_EQ (reported.front (), hash);

	// Further votes do not report the block again, whether they add weight or repeat a representative
	insert_routed (vote_cache, nano::test::make_vote (rep3, { hash }, 1024 * 1024), nano::vote_code::indeterminate);
	insert_routed (vote_cache, nano::test::make_vote (rep1, { hash }, 2 * 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_EQ (reported.size (), 1);
}

/*
 * A vote for several hashes reports each block whose tally it brings up to the threshold, and only those.
 * One hash already has weight 60 cached, the other has none; a vote of weight 60 for both lifts only the first to the threshold of 100.
 */
TEST (vote_cache, threshold_reached_per_hash)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };

	// Collect the reports, against a fixed threshold
	std::deque<nano::block_hash> reported;
	vote_cache.threshold_reached.add ([&reported] (nano::block_hash const & hash) {
		reported.push_back (hash);
	});
	vote_cache.threshold_query = [] () {
		return nano::uint128_t{ 100 };
	};

	auto const backed = nano::test::random_hash ();
	auto const fresh = nano::test::random_hash ();
	auto const rep1 = create_rep (60);
	auto const rep2 = create_rep (60);

	// Only one of the two hashes has votes cached so far
	insert_routed (vote_cache, nano::test::make_vote (rep1, { backed }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_TRUE (reported.empty ());

	// A single vote for both reports the one it lifts to the threshold
	insert_routed (vote_cache, nano::test::make_vote (rep2, { backed, fresh }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_EQ (reported.size (), 1);
	ASSERT_EQ (reported.front (), backed);
}

/*
 * Votes that an election counts never report their block, since a block with an election is not missing.
 * The same votes that reach the threshold in the test above are cached here as matched by an election, and a later
 * unmatched vote finds the tally above the threshold already, so it reports nothing either.
 */
TEST (vote_cache, threshold_not_reported_with_election)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };

	// Collect the reports, against a fixed threshold
	std::deque<nano::block_hash> reported;
	vote_cache.threshold_reached.add ([&reported] (nano::block_hash const & hash) {
		reported.push_back (hash);
	});
	vote_cache.threshold_query = [] () {
		return nano::uint128_t{ 100 };
	};

	auto const hash = nano::test::random_hash ();
	auto const rep1 = create_rep (60);
	auto const rep2 = create_rep (60);
	auto const rep3 = create_rep (60);

	// The tally passes the threshold with votes an election counted
	insert_routed (vote_cache, nano::test::make_vote (rep1, { hash }, 1024 * 1024), nano::vote_code::vote);
	insert_routed (vote_cache, nano::test::make_vote (rep2, { hash }, 1024 * 1024), nano::vote_code::vote);
	ASSERT_TRUE (reported.empty ());

	// An unmatched vote on top of them does not bring the tally up to the threshold, it is there already
	insert_routed (vote_cache, nano::test::make_vote (rep3, { hash }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_TRUE (reported.empty ());
}

/*
 * Without a threshold to compare against nothing is reported, however much weight a block gathers.
 */
TEST (vote_cache, threshold_unset)
{
	nano::test::system system;
	nano::vote_cache_config cfg;
	nano::vote_cache vote_cache{ cfg, system.stats };

	// Collect the reports, with no threshold given
	std::deque<nano::block_hash> reported;
	vote_cache.threshold_reached.add ([&reported] (nano::block_hash const & hash) {
		reported.push_back (hash);
	});

	auto const hash = nano::test::random_hash ();
	auto const rep1 = create_rep (1000);

	// The vote is cached and nothing is reported
	insert_routed (vote_cache, nano::test::make_vote (rep1, { hash }, 1024 * 1024), nano::vote_code::indeterminate);
	ASSERT_TRUE (vote_cache.contains (hash));
	ASSERT_TRUE (reported.empty ());
}
