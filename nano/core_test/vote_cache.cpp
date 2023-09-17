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

std::shared_ptr<nano::vote> create_vote (nano::keypair & key, uint64_t timestamp, uint8_t duration, std::vector<nano::block_hash> hashes)
{
	return nano::test::make_vote (key, hashes, timestamp, duration);
}

nano::block_hash random_hash ()
{
	nano::block_hash random_hash;
	nano::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
	return random_hash;
}

nano::vote_cache::config make_config (std::size_t max_size = 1024)
{
	nano::vote_cache::config cfg{};
	cfg.max_size = max_size;
	return cfg;
}
}

TEST (vote_cache, construction)
{
	nano::vote_cache vote_cache{ make_config () };
	ASSERT_EQ (0, vote_cache.cache_size ());
	ASSERT_TRUE (vote_cache.cache_empty ());
	auto hash1 = random_hash ();
	ASSERT_FALSE (vote_cache.find (hash1));
}

/*
 * Inserts single hash to cache, ensures it can be retrieved and dequeued
 */
TEST (vote_cache, insert_one_hash)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto rep1 = create_rep (7);
	auto hash1 = random_hash ();
	auto vote1 = create_vote (rep1, 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	ASSERT_EQ (1, vote_cache.cache_size ());
	ASSERT_TRUE (vote_cache.find (hash1));
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	ASSERT_EQ (peek1->hash (), hash1);
	ASSERT_EQ (peek1->voters ().size (), 1);
	ASSERT_EQ (peek1->voters ().front ().representative, rep1.pub); // account
	ASSERT_EQ (peek1->voters ().front ().timestamp, 1024 * 1024); // timestamp
	ASSERT_EQ (peek1->tally (), 7);
}

/*
 * Inserts multiple votes for single hash
 * Ensures all of them can be retrieved and that tally is properly accumulated
 */
TEST (vote_cache, insert_one_hash_many_votes)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = random_hash ();
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto vote1 = create_vote (rep1, 1 * 1024 * 1024, 0, { hash1 });
	auto vote2 = create_vote (rep2, 2 * 1024 * 1024, 0, { hash1 });
	auto vote3 = create_vote (rep3, 3 * 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	vote_cache.vote (vote2->hashes.front (), vote2);
	vote_cache.vote (vote3->hashes.front (), vote3);
	// We have 3 votes but for a single hash, so just one entry in vote cache
	ASSERT_EQ (1, vote_cache.cache_size ());
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	ASSERT_EQ (peek1->voters ().size (), 3);
	// Tally must be the sum of rep weights
	ASSERT_EQ (peek1->tally (), 7 + 9 + 11);
}

/*
 * Inserts multiple votes for multiple hashes
 * Ensures all of them can be retrieved and that queue returns the highest tally entries first
 */
TEST (vote_cache, insert_many_hashes_many_votes)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	// There will be 3 random hashes to vote for
	auto hash1 = random_hash ();
	auto hash2 = random_hash ();
	auto hash3 = random_hash ();
	// There will be 4 reps with different weights
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto rep4 = create_rep (13);
	// Votes: rep1 > hash1, rep2 > hash2, rep3 > hash3, rep4 > hash1 (the same as rep1)
	auto vote1 = create_vote (rep1, 1024 * 1024, 0, { hash1 });
	auto vote2 = create_vote (rep2, 1024 * 1024, 0, { hash2 });
	auto vote3 = create_vote (rep3, 1024 * 1024, 0, { hash3 });
	auto vote4 = create_vote (rep4, 1024 * 1024, 0, { hash1 });
	// Insert first 3 votes in cache
	vote_cache.vote (vote1->hashes.front (), vote1);
	vote_cache.vote (vote2->hashes.front (), vote2);
	vote_cache.vote (vote3->hashes.front (), vote3);
	// Ensure all of those are properly inserted
	ASSERT_EQ (3, vote_cache.cache_size ());
	ASSERT_TRUE (vote_cache.find (hash1));
	ASSERT_TRUE (vote_cache.find (hash2));
	ASSERT_TRUE (vote_cache.find (hash3));

	// Ensure that first entry in queue is the one for hash3 (rep3 has the highest weight of the first 3 reps)
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	ASSERT_EQ (peek1->voters ().size (), 1);
	ASSERT_EQ (peek1->tally (), 11);
	ASSERT_EQ (peek1->hash (), hash3);

	// Now add a vote from rep4 with the highest voting weight
	vote_cache.vote (vote4->hashes.front (), vote4);

	// Ensure that the first entry in queue is now the one for hash1 (rep1 + rep4 tally weight)
	auto pop1 = vote_cache.pop ();
	ASSERT_TRUE (pop1);
	ASSERT_EQ ((*pop1).voters ().size (), 2);
	ASSERT_EQ ((*pop1).tally (), 7 + 13);
	ASSERT_EQ ((*pop1).hash (), hash1);
	ASSERT_TRUE (vote_cache.find (hash1)); // Only pop from queue, votes should still be stored in cache

	// After popping the previous entry, the next entry in queue should be hash3 (rep3 tally weight)
	auto pop2 = vote_cache.pop ();
	ASSERT_EQ ((*pop2).voters ().size (), 1);
	ASSERT_EQ ((*pop2).tally (), 11);
	ASSERT_EQ ((*pop2).hash (), hash3);
	ASSERT_TRUE (vote_cache.find (hash3));

	// And last one should be hash2 with rep2 tally weight
	auto pop3 = vote_cache.pop ();
	ASSERT_EQ ((*pop3).voters ().size (), 1);
	ASSERT_EQ ((*pop3).tally (), 9);
	ASSERT_EQ ((*pop3).hash (), hash2);
	ASSERT_TRUE (vote_cache.find (hash2));

	ASSERT_TRUE (vote_cache.queue_empty ());
}

/*
 * Ensure that duplicate votes are ignored
 */
TEST (vote_cache, insert_duplicate)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = create_vote (rep1, 1 * 1024 * 1024, 0, { hash1 });
	auto vote2 = create_vote (rep1, 1 * 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	vote_cache.vote (vote2->hashes.front (), vote2);
	ASSERT_EQ (1, vote_cache.cache_size ());
}

/*
 * Ensure that when processing vote from a representative that is already cached, we always update to the vote with the highest timestamp
 */
TEST (vote_cache, insert_newer)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = create_vote (rep1, 1 * 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	auto vote2 = create_vote (rep1, nano::vote::timestamp_max, nano::vote::duration_max, { hash1 });
	vote_cache.vote (vote2->hashes.front (), vote2);
	auto peek2 = vote_cache.peek ();
	ASSERT_TRUE (peek2);
	ASSERT_EQ (1, vote_cache.cache_size ());
	ASSERT_EQ (1, peek2->voters ().size ());
	// Second entry should have timestamp greater than the first one
	ASSERT_GT (peek2->voters ().front ().timestamp, peek1->voters ().front ().timestamp);
	ASSERT_EQ (peek2->voters ().front ().timestamp, std::numeric_limits<uint64_t>::max ()); // final timestamp
}

/*
 * Ensure that when processing vote from a representative that is already cached, votes with older timestamp are ignored
 */
TEST (vote_cache, insert_older)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = random_hash ();
	auto rep1 = create_rep (9);
	auto vote1 = create_vote (rep1, 2 * 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	auto vote2 = create_vote (rep1, 1 * 1024 * 1024, 0, { hash1 });
	vote_cache.vote (vote2->hashes.front (), vote2);
	auto peek2 = vote_cache.peek ();
	ASSERT_TRUE (peek2);
	ASSERT_EQ (1, vote_cache.cache_size ());
	ASSERT_EQ (1, peek2->voters ().size ());
	ASSERT_EQ (peek2->voters ().front ().timestamp, peek1->voters ().front ().timestamp); // timestamp2 == timestamp1
}

/*
 * Ensure that erase functionality works
 */
TEST (vote_cache, erase)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	auto hash1 = random_hash ();
	auto hash2 = random_hash ();
	auto hash3 = random_hash ();
	auto rep1 = create_rep (7);
	auto rep2 = create_rep (9);
	auto rep3 = create_rep (11);
	auto rep4 = create_rep (13);
	auto vote1 = create_vote (rep1, 1024 * 1024, 0, { hash1 });
	auto vote2 = create_vote (rep2, 1024 * 1024, 0, { hash2 });
	auto vote3 = create_vote (rep3, 1024 * 1024, 0, { hash3 });
	vote_cache.vote (vote1->hashes.front (), vote1);
	vote_cache.vote (vote2->hashes.front (), vote2);
	vote_cache.vote (vote3->hashes.front (), vote3);
	ASSERT_EQ (3, vote_cache.cache_size ());
	ASSERT_TRUE (vote_cache.find (hash1));
	ASSERT_TRUE (vote_cache.find (hash2));
	ASSERT_TRUE (vote_cache.find (hash3));
	vote_cache.erase (hash2);
	ASSERT_EQ (2, vote_cache.cache_size ());
	ASSERT_TRUE (vote_cache.find (hash1));
	ASSERT_FALSE (vote_cache.find (hash2));
	ASSERT_TRUE (vote_cache.find (hash3));
	vote_cache.erase (hash1);
	vote_cache.erase (hash3);
	ASSERT_FALSE (vote_cache.find (hash1));
	ASSERT_FALSE (vote_cache.find (hash2));
	ASSERT_FALSE (vote_cache.find (hash3));
	ASSERT_TRUE (vote_cache.cache_empty ());
}

/*
 * Ensure that when cache is overfilled, we remove the oldest entries first
 */
TEST (vote_cache, overfill)
{
	// Create a vote cache with max size set to 1024
	nano::vote_cache vote_cache{ make_config (1024) };
	vote_cache.rep_weight_query = rep_weight_query ();
	const int count = 16 * 1024;
	for (int n = 0; n < count; ++n)
	{
		// The more recent the vote, the less voting weight it has
		auto rep1 = create_rep (count - n);
		auto hash1 = random_hash ();
		auto vote1 = create_vote (rep1, 1024 * 1024, 0, { hash1 });
		vote_cache.vote (vote1->hashes.front (), vote1);
	}
	ASSERT_LT (vote_cache.cache_size (), count);
	auto peek1 = vote_cache.peek ();
	ASSERT_TRUE (peek1);
	// Check that oldest votes are dropped first
	ASSERT_EQ (peek1->tally (), 1024);
}

/*
 * Check that when a single vote cache entry is overfilled, it ignores any new votes
 */
TEST (vote_cache, overfill_entry)
{
	nano::vote_cache vote_cache{ make_config () };
	vote_cache.rep_weight_query = rep_weight_query ();
	const int count = 1024;
	auto hash1 = random_hash ();
	for (int n = 0; n < count; ++n)
	{
		auto rep1 = create_rep (9);
		auto vote1 = create_vote (rep1, 1024 * 1024, 0, { hash1 });
		vote_cache.vote (vote1->hashes.front (), vote1);
	}
	ASSERT_EQ (1, vote_cache.cache_size ());
}