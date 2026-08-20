#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/keypair.hpp>
#include <nano/lib/vote.hpp>
#include <nano/secure/election_ballot.hpp>
#include <nano/test_common/random.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>

using namespace std::chrono_literals;

namespace
{
std::chrono::steady_clock::time_point constexpr epoch{};

// Representative weights for one test, injected into the ballot as its weight query
struct test_reps
{
	std::unordered_map<nano::account, nano::uint128_t> weights;

	nano::election_ballot::weight_fn query ()
	{
		return [this] (nano::account const & rep) {
			auto existing = weights.find (rep);
			return existing != weights.end () ? existing->second : nano::uint128_t{ 0 };
		};
	}

	nano::account rep (nano::uint128_t weight)
	{
		nano::keypair key;
		weights[key.pub] = weight;
		return key.pub;
	}
};

std::shared_ptr<nano::block> create_block ()
{
	nano::keypair key;
	nano::block_builder builder;
	return builder.send ()
	.previous (nano::test::random_hash ())
	.destination (key.pub)
	.balance (0)
	.sign (key.prv, key.pub)
	.work (0)
	.build ();
}

// Blocks ordered by ascending hash, so tests can rely on a known hash order
std::vector<std::shared_ptr<nano::block>> create_blocks_hash_ordered (size_t count)
{
	std::vector<std::shared_ptr<nano::block>> blocks;
	blocks.reserve (count);
	for (size_t i = 0; i < count; ++i)
	{
		blocks.push_back (create_block ());
	}
	std::sort (blocks.begin (), blocks.end (), [] (auto const & lhs, auto const & rhs) {
		return lhs->hash () < rhs->hash ();
	});
	return blocks;
}

// Sum of the tallied weight over held blocks, the participation measure evaluate gates the winner on
nano::uint128_t total_weight (nano::election_ballot const & ballot)
{
	nano::uint128_t result{ 0 };
	for (auto const & [key, block] : ballot.tally ())
	{
		result += key.weight;
	}
	return result;
}
}

/*
 * tally_map
 */

/*
 * The tally order is enforced by the container itself, not by whoever builds it: entries inserted in arbitrary order come out heaviest first, and equal weights come out higher hash first.
 * Every consumer of a tally (winner selection, quorum checks, RPC listings) inherits this order for free, so no code path can produce a differently ordered tally.
 */
TEST (tally_map, order)
{
	// Insert in deliberately shuffled order, including two entries tied at weight 5
	nano::tally_map tally;
	tally.emplace (nano::tally_key{ 5, nano::block_hash{ 1 } }, nullptr);
	tally.emplace (nano::tally_key{ 3, nano::block_hash{ 9 } }, nullptr);
	tally.emplace (nano::tally_key{ 7, nano::block_hash{ 1 } }, nullptr);
	tally.emplace (nano::tally_key{ 5, nano::block_hash{ 2 } }, nullptr);

	std::vector<nano::tally_key> keys;
	for (auto const & [key, block] : tally)
	{
		keys.push_back (key);
	}

	// Expected iteration order: (7,1), (5,2), (5,1), (3,9)
	ASSERT_EQ (4, keys.size ());
	ASSERT_EQ (7, keys[0].weight);
	ASSERT_EQ (nano::block_hash{ 1 }, keys[0].hash);
	ASSERT_EQ (5, keys[1].weight);
	ASSERT_EQ (nano::block_hash{ 2 }, keys[1].hash); // Higher hash first among equal weights
	ASSERT_EQ (5, keys[2].weight);
	ASSERT_EQ (nano::block_hash{ 1 }, keys[2].hash);
	ASSERT_EQ (3, keys[3].weight);
	ASSERT_EQ (nano::block_hash{ 9 }, keys[3].hash);
}

/*
 * Two forks with exactly equal weight must both remain visible in the tally, since collapsing them would hide a live fork race from quorum checks.
 * The hash is part of the map key, so equal-weight entries stay distinct and only an exact (weight, hash) duplicate collapses.
 */
TEST (tally_map, equal_weights_distinct)
{
	nano::tally_map tally;
	// Same weight under two different hashes forms two distinct keys
	tally.emplace (nano::tally_key{ 5, nano::block_hash{ 1 } }, nullptr);
	tally.emplace (nano::tally_key{ 5, nano::block_hash{ 2 } }, nullptr);
	tally.emplace (nano::tally_key{ 5, nano::block_hash{ 2 } }, nullptr); // Exact duplicate collapses
	ASSERT_EQ (2, tally.size ());
}

/*
 * vote_info
 */

/*
 * A vote is final exactly when it carries the reserved maximum timestamp.
 * Ordinary timestamps, including the minimum, are not final.
 */
TEST (vote_info, final)
{
	// Default-constructed and ordinary timestamps are not final, only the reserved maximum is
	nano::vote_info info;
	ASSERT_FALSE (info.final ());
	info.timestamp = nano::vote::timestamp_min;
	ASSERT_FALSE (info.final ());
	info.timestamp = nano::vote::timestamp_final;
	ASSERT_TRUE (info.final ());
}

/*
 * vote_with_weight_info
 */

TEST (vote_with_weight_info, defaults)
{
	nano::vote_with_weight_info info;
	ASSERT_TRUE (info.representative.is_zero ());
	ASSERT_EQ (epoch, info.arrival);
	ASSERT_EQ (0, info.timestamp);
	ASSERT_TRUE (info.hash.is_zero ());
	ASSERT_EQ (0, info.weight);
}

/*
 * calculate_vote_cooldown
 */

/*
 * The cooldown between subsequent votes from the same representative scales with the rep's share of the online stake: heavy reps may re-vote quickly since their votes move elections, while light reps are throttled harder to bound the vote traffic they can generate.
 */
TEST (calculate_vote_cooldown, tiers)
{
	// With an online stake of 100 the tier boundaries sit at weight 5 (5%) and weight 1 (1%), both exclusive
	nano::uint128_t const online_stake{ 100 };
	ASSERT_EQ (1s, nano::calculate_vote_cooldown (6, online_stake)); // More than 5%
	ASSERT_EQ (5s, nano::calculate_vote_cooldown (2, online_stake)); // More than 1%
	ASSERT_EQ (15s, nano::calculate_vote_cooldown (1, online_stake)); // The rest
}

/*
 * election_ballot
 */

/*
 * A fresh ballot starts with the initial block as its winner and sole held block, and with no recorded votes.
 * The winner is defined from the very first moment, so there is never a state in which the election has nothing to broadcast or vote for.
 */
TEST (election_ballot, construction)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };

	// The initial block is immediately the winner and retrievable through the block queries
	ASSERT_EQ (initial, ballot.winner ());
	ASSERT_TRUE (ballot.contains_block (initial->hash ()));
	ASSERT_EQ (initial, ballot.find_block (initial->hash ()));
	ASSERT_EQ (nullptr, ballot.find_block (nano::test::random_hash ()));
	ASSERT_EQ (1, ballot.block_count ());

	// No votes are recorded yet, all vote queries are well-defined and empty
	ASSERT_EQ (0, ballot.voter_count ());
	ASSERT_TRUE (ballot.votes ().empty ());
	ASSERT_TRUE (ballot.tally ().empty ());
	ASSERT_FALSE (ballot.find_vote (nano::keypair ().pub).has_value ());
}

/*
 * Evaluating a ballot that has received no votes is a well-defined no-op round: the winner remains the initial block, all weights are zero and no form of quorum is reached.
 * Callers do not need to special-case the empty election.
 */
TEST (election_ballot, evaluate_no_votes)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };

	// The threshold value is irrelevant, without votes nothing can reach it
	auto round = ballot.evaluate (10);
	ASSERT_TRUE (ballot.tally ().empty ());
	ASSERT_EQ (initial, round.winner);
	ASSERT_EQ (0, round.winner_weight);
	ASSERT_EQ (0, round.final_winner_weight);
	ASSERT_EQ (0, total_weight (ballot));
	ASSERT_FALSE (round.quorum);
	ASSERT_FALSE (round.final_quorum);
}

/*
 * An accepted vote is recorded verbatim under the representative's account: arrival time, vote timestamp and voted-for hash are all retrievable exactly as submitted.
 * This recorded state is what all later replay and cooldown decisions for the rep are measured against.
 */
TEST (election_ballot, vote_recorded)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// A vote with the minimum timestamp arrives at epoch + 1s
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, initial->hash (), 0s, epoch + 1s));
	ASSERT_EQ (1, ballot.voter_count ());

	// The recorded vote reproduces every submitted field
	auto info = ballot.find_vote (rep);
	ASSERT_TRUE (info.has_value ());
	ASSERT_EQ (epoch + 1s, info->arrival);
	ASSERT_EQ (nano::vote::timestamp_min, info->timestamp);
	ASSERT_EQ (initial->hash (), info->hash);
}

/*
 * A vote with a lower timestamp than the rep's recorded vote is a replay of stale information and must be rejected without modifying the recorded vote.
 * This is the basic protection that stops rebroadcasted old votes from rolling a representative's stance backwards.
 */
TEST (election_ballot, vote_replay_lower_timestamp)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// Record timestamp 2 first, then replay the older timestamp 1
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 2, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_min * 1, initial->hash (), 0s, epoch));

	// The recorded vote is unchanged
	ASSERT_EQ (nano::vote::timestamp_min * 2, ballot.find_vote (rep)->timestamp);
}

/*
 * Votes carrying the same timestamp are ordered by hash: only a strictly higher hash may replace the recorded vote, the same or a lower hash is a replay.
 * Together with the timestamp ordering this makes a rep's vote sequence a strict total order, so any two conflicting votes from one rep always resolve the same way on every node.
 */
TEST (election_ballot, vote_equal_timestamp_tiebreak_by_hash)
{
	test_reps reps;

	// Start from the middle block so both a lower and a higher hash exist to vote for
	auto blocks = create_blocks_hash_ordered (3);
	auto & low = blocks[0];
	auto & mid = blocks[1];
	auto & high = blocks[2];
	nano::election_ballot ballot{ mid, reps.query () };
	auto rep = reps.rep (10);

	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, mid->hash (), 0s, epoch));

	// Same timestamp: the same or a lower hash replays, only a higher hash advances
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_min, mid->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_min, low->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, high->hash (), 0s, epoch));
	ASSERT_EQ (high->hash (), ballot.find_vote (rep)->hash);
}

/*
 * A newer vote arriving within the rep's cooldown window is ignored: it is valid but throttled, and the recorded vote stays untouched, so an ignored vote can be resubmitted later.
 * Once the full cooldown has elapsed since the recorded vote arrived, the newer vote is accepted.
 */
TEST (election_ballot, vote_cooldown_window)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// The first vote at epoch opens a 10s cooldown window
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 1, initial->hash (), 10s, epoch));

	// A newer vote within the cooldown window is ignored and leaves the recorded vote unchanged
	ASSERT_EQ (nano::election_ballot::vote_result::ignored, ballot.vote (rep, nano::vote::timestamp_min * 2, initial->hash (), 10s, epoch + 5s));
	ASSERT_EQ (nano::vote::timestamp_min * 1, ballot.find_vote (rep)->timestamp);

	// The window boundary is inclusive: exactly at cooldown expiry the newer vote is accepted
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 2, initial->hash (), 10s, epoch + 10s));
	ASSERT_EQ (nano::vote::timestamp_min * 2, ballot.find_vote (rep)->timestamp);
}

/*
 * A cooldown of zero disables throttling entirely: consecutive votes from the same rep are all accepted as long as they advance the (timestamp, hash) order.
 * Callers use this for votes that were already delayed elsewhere, such as votes replayed from the vote cache.
 */
TEST (election_ballot, vote_cooldown_zero_disables_throttling)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// Three votes at the same instant, each advancing only the timestamp
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 1, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 2, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 3, initial->hash (), 0s, epoch));
}

/*
 * A representative's first final vote is accepted immediately even inside the cooldown window.
 * Final votes are the terminal step of consensus, and delaying them would delay confirmation without any spam-protection benefit, since a rep can only make this transition once.
 */
TEST (election_ballot, vote_first_final_bypasses_cooldown)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// A normal vote opens a 10s cooldown window
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, initial->hash (), 10s, epoch));

	// The rep's first final vote is accepted immediately, despite the cooldown window
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_final, initial->hash (), 10s, epoch + 1s));
	ASSERT_TRUE (ballot.find_vote (rep)->final ());
}

/*
 * A final vote replacing another final vote gets no cooldown bypass.
 * All final votes share the same timestamp, so such a switch is only possible towards a higher hash, and it is throttled like any ordinary re-vote: switching finals is not a step towards confirmation and must not be cheaper than switching normal votes.
 * A final vote for a lower hash can never be accepted at all.
 */
TEST (election_ballot, vote_final_replacing_final_respects_cooldown)
{
	test_reps reps;
	auto blocks = create_blocks_hash_ordered (2);
	auto & low = blocks[0];
	auto & high = blocks[1];
	nano::election_ballot ballot{ low, reps.query () };
	auto rep = reps.rep (10);

	// The rep final-votes the lower-hash block first, opening a 10s cooldown window
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_final, low->hash (), 10s, epoch));

	// A final vote for a lower or equal hash can never advance
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_final, low->hash (), 10s, epoch + 20s));

	// A final vote replacing another final vote gets no cooldown bypass
	ASSERT_EQ (nano::election_ballot::vote_result::ignored, ballot.vote (rep, nano::vote::timestamp_final, high->hash (), 10s, epoch + 5s));

	// After the cooldown the switch towards the higher hash passes the tie-break
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_final, high->hash (), 10s, epoch + 10s));
}

/*
 * A vote for a hash the ballot does not hold is still recorded: it occupies the rep's single vote slot and anchors replay protection, but contributes nothing to the tally or to the participation weight, since it expresses no preference between the held blocks.
 * Recording it means a replayed older vote cannot sneak in just because the rep's preferred block is unknown here.
 */
TEST (election_ballot, vote_unheld_hash_recorded_not_tallied)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);
	auto unheld = nano::test::random_hash ();

	// The vote references a hash this ballot has never seen, it is recorded but carries no tally weight
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 2, unheld, 0s, epoch));
	ASSERT_EQ (1, ballot.voter_count ());
	ASSERT_TRUE (ballot.tally ().empty ());

	auto round = ballot.evaluate (5);
	ASSERT_EQ (0, total_weight (ballot));
	ASSERT_EQ (initial, round.winner);

	// A replayed older vote is still rejected, the unheld vote anchors the rep's ordering
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_min * 1, initial->hash (), 0s, epoch));
}

/*
 * A vote may arrive before the block it refers to.
 * The vote is kept while its hash is unheld, and the moment the block is inserted the recorded weight starts counting, without the rep having to vote again.
 * Here the parked weight is enough to immediately flip the winner to the late block.
 */
TEST (election_ballot, vote_before_block)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	auto rep = reps.rep (10);

	// The vote arrives before its block, while the hash is unheld the weight stays out of the tally
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, fork->hash (), 0s, epoch));
	ASSERT_EQ (0, total_weight (ballot));

	// Inserting the block makes the parked weight count: 10 passes the threshold of 5 and flips the winner
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);
	auto round = ballot.evaluate (5);
	ASSERT_EQ (10, total_weight (ballot));
	ASSERT_EQ (fork, round.winner);
}

/*
 * Inserting a block whose hash is already held refreshes the stored block object, e.g. when the same block arrives again with a higher work value.
 * The winner is resolved through the held blocks by hash, so a refresh of the winning block is immediately visible through winner () without any separate synchronization.
 */
TEST (election_ballot, insert_updates_existing_block)
{
	test_reps reps;
	nano::keypair key;
	nano::block_builder builder;

	// Building the same fields twice yields the same hash but two distinct block objects
	auto build = [&] () {
		return builder.send ()
		.previous (123)
		.destination (key.pub)
		.balance (0)
		.sign (key.prv, key.pub)
		.work (0)
		.build ();
	};
	auto initial = build ();
	auto duplicate = build ();
	ASSERT_EQ (initial->hash (), duplicate->hash ());
	ASSERT_NE (initial, duplicate);

	nano::election_ballot ballot{ initial, reps.query () };
	auto result = ballot.insert (duplicate);
	ASSERT_EQ (nano::election_ballot::insert_outcome::updated, result.outcome);
	ASSERT_EQ (nullptr, result.evicted);
	ASSERT_EQ (1, ballot.block_count ());

	// The stored block and the winner both reflect the refreshed contents
	ASSERT_EQ (duplicate, ballot.find_block (initial->hash ()));
	ASSERT_EQ (duplicate, ballot.winner ());
}

/*
 * The block limit is enforced by the ballot itself: once full, a fork without backing weight is rejected and the block count never exceeds the maximum.
 * There is no way for callers to bypass the capacity check, since insertion is the only way blocks enter.
 */
TEST (election_ballot, insert_rejected_when_full)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 3 };

	// Capacity 3: the initial block plus two forks fill the ballot
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (create_block ()).outcome);
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (create_block ()).outcome);
	ASSERT_EQ (3, ballot.block_count ());

	// Without backing weight the new fork cannot displace anything
	auto fork = create_block ();
	ASSERT_EQ (nano::election_ballot::insert_outcome::rejected, ballot.insert (fork).outcome);
	ASSERT_EQ (3, ballot.block_count ());
	ASSERT_FALSE (ballot.contains_block (fork->hash ()));
}

/*
 * When the ballot is full, a new fork backed by more externally observed weight than the weakest held non-winner evicts exactly that weakest block, in a single atomic step.
 * The evicted block is returned so the caller can undo its external registrations (vote routing, network filters).
 */
TEST (election_ballot, insert_replaces_weakest)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork_strong = create_block ();
	auto fork_weak = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 3 };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork_strong).outcome);
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork_weak).outcome);

	// Tallied weights: fork_strong 5, fork_weak 3, the winner remains unvoted
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (5), nano::vote::timestamp_min, fork_strong->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (3), nano::vote::timestamp_min, fork_weak->hash (), 0s, epoch));

	// Cached weight 4 beats the weakest fork (3) and takes exactly its place, the stronger fork (5) is untouched
	auto incoming = create_block ();
	auto result = ballot.insert (incoming, 4);
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, result.outcome);
	ASSERT_EQ (fork_weak, result.evicted);
	ASSERT_EQ (3, ballot.block_count ());
	ASSERT_TRUE (ballot.contains_block (incoming->hash ()));
	ASSERT_FALSE (ballot.contains_block (fork_weak->hash ()));
}

/*
 * Eviction requires strictly more backing weight than the weakest block holds.
 * On equal weight the incumbent wins, so two equally backed forks cannot evict each other back and forth and a fork needs a real weight advantage to enter a full ballot.
 */
TEST (election_ballot, insert_replace_requires_strictly_more_weight)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 2 };

	// The sole non-winner fork holds 5 tallied weight
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (5), nano::vote::timestamp_min, fork->hash (), 0s, epoch));

	// Equal backing weight is not enough, only strictly more evicts
	ASSERT_EQ (nano::election_ballot::insert_outcome::rejected, ballot.insert (create_block (), 5).outcome);
	auto result = ballot.insert (create_block (), 6);
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, result.outcome);
	ASSERT_EQ (fork, result.evicted);
}

/*
 * The current winner can never be evicted, no matter how much weight backs the incoming fork and even when the winner itself has no votes at all.
 * Losing the winner would leave the election broadcasting and voting for a block it no longer holds; a stronger fork displaces the winner only through the tally, never through eviction.
 */
TEST (election_ballot, insert_never_evicts_winner)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 2 };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (5), nano::vote::timestamp_min, fork->hash (), 0s, epoch));

	// Cached weight 100 dwarfs everything held, yet the unvoted winner survives and the 5-weight fork goes
	auto result = ballot.insert (create_block (), 100);
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, result.outcome);
	ASSERT_EQ (fork, result.evicted);
	ASSERT_TRUE (ballot.contains_block (initial->hash ()));
	ASSERT_EQ (initial, ballot.winner ());
}

/*
 * With a capacity of one the winner is the only held block, so a full ballot has nothing that may be evicted and every new fork is rejected regardless of its backing weight.
 * Degenerate limits fail safe instead of touching the winner.
 */
TEST (election_ballot, insert_rejected_when_only_winner_held)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 1 };

	// With the winner as the only held block there is nothing that may be evicted
	ASSERT_EQ (nano::election_ballot::insert_outcome::rejected, ballot.insert (create_block (), 100).outcome);
	ASSERT_EQ (1, ballot.block_count ());
}

/*
 * When several eviction candidates have equal (here: zero) tallied weight, the one with the lowest hash is evicted.
 * The choice is deterministic, so every node holding the same votes and blocks evicts the same fork.
 */
TEST (election_ballot, insert_eviction_tie_broken_by_lower_hash)
{
	test_reps reps;
	auto initial = create_block ();
	auto forks = create_blocks_hash_ordered (2);

	// Capacity 3 fills up with the winner and two unvoted forks of known hash order
	nano::election_ballot ballot{ initial, reps.query (), 3 };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (forks[0]).outcome);
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (forks[1]).outcome);

	// Both forks are unvoted, the lower hash is evicted first
	auto result = ballot.insert (create_block (), 1);
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, result.outcome);
	ASSERT_EQ (forks[0], result.evicted);
}

/*
 * Evicting a block does not erase the votes recorded for it.
 * The votes become unheld-hash votes: they stop counting in the tally but keep anchoring their reps' replay protection, so a stale older vote cannot be re-recorded just because the block it lost to was evicted.
 * If the evicted fork later re-enters the ballot, the retained votes count again immediately, making eviction fully reversible.
 */
TEST (election_ballot, eviction_keeps_votes)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query (), 2 };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// The rep backs the only non-winner fork with weight 5 at timestamp 2
	auto rep = reps.rep (5);
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min * 2, fork->hash (), 0s, epoch));

	// Cached weight 6 outweighs the fork's 5 and evicts it
	auto result = ballot.insert (create_block (), 6);
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, result.outcome);
	ASSERT_EQ (fork, result.evicted);

	// The vote for the evicted fork is retained: it keeps its replay protection and stays out of the tally
	ASSERT_EQ (1, ballot.voter_count ());
	ASSERT_EQ (fork->hash (), ballot.find_vote (rep)->hash);
	ASSERT_EQ (nano::election_ballot::vote_result::replay, ballot.vote (rep, nano::vote::timestamp_min * 1, initial->hash (), 0s, epoch));
	ASSERT_EQ (0, total_weight (ballot));

	// Re-inserting the evicted fork restores its recorded weight
	ASSERT_EQ (nano::election_ballot::insert_outcome::replaced, ballot.insert (fork, 6).outcome);
	ASSERT_EQ (5, total_weight (ballot));
}

/*
 * The winner only follows the tally leader once the total tallied weight reaches the quorum threshold.
 * A fork may lead the tally, but with too little overall participation the winner does not move: this keeps the winner stable while only a few early votes are in, instead of flapping after every vote.
 * Once participation is sufficient the leader takes over and the winner stays with it.
 */
TEST (election_ballot, evaluate_winner_gate)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// Threshold 15: a single 10-weight vote stays below the gate, two such votes pass it
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (10), nano::vote::timestamp_min, fork->hash (), 0s, epoch));

	// The fork leads the tally, but with too little participation the winner does not move
	auto round1 = ballot.evaluate (15);
	ASSERT_EQ (initial, round1.winner);
	ASSERT_EQ (initial, ballot.winner ());
	ASSERT_EQ (0, round1.winner_weight); // The winner itself has no votes
	ASSERT_EQ (10, total_weight (ballot));
	ASSERT_FALSE (round1.quorum);

	// Enough participation lets the leader take over
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (10), nano::vote::timestamp_min, fork->hash (), 0s, epoch));
	auto round2 = ballot.evaluate (15);
	ASSERT_EQ (fork, round2.winner);
	ASSERT_EQ (fork, ballot.winner ());
	ASSERT_EQ (20, round2.winner_weight);

	// The winner stays with the leader on subsequent evaluations
	auto round3 = ballot.evaluate (15);
	ASSERT_EQ (fork, round3.winner);
}

/*
 * Only weight behind held blocks counts towards the participation gate.
 * Weight parked on unheld hashes expresses no preference between the actual candidates, so counting it would let the winner move on the strength of votes that back nothing in this election.
 * Here the combined weight would pass the gate, but the held-block weight alone does not, and the winner stays.
 */
TEST (election_ballot, evaluate_gate_counts_only_held_blocks)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// 10 weight backs the held fork and another 10 backs an unknown hash, only the former participates
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (10), nano::vote::timestamp_min, fork->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (10), nano::vote::timestamp_min, nano::test::random_hash (), 0s, epoch));

	// Held-block participation is 10, below the threshold of 15, so the winner may not move yet
	auto round = ballot.evaluate (15);
	ASSERT_EQ (10, total_weight (ballot));
	ASSERT_EQ (initial, round.winner);
}

/*
 * Quorum requires the winner to lead the runner-up by at least the quorum threshold.
 * The margin formulation is the safety argument: with a lead of a full threshold, even if all online weight that has not been tallied yet piled onto the runner-up, it could no longer overtake the winner.
 * An unopposed winner above the threshold has quorum; a close race does not.
 */
TEST (election_ballot, evaluate_quorum_margin)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// A single 30-weight vote passes the gate (30 >= 10) and leads unopposed by its full weight
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (30), nano::vote::timestamp_min, fork->hash (), 0s, epoch));
	auto round1 = ballot.evaluate (10);
	ASSERT_EQ (fork, round1.winner);
	ASSERT_TRUE (round1.quorum); // Unopposed winner with weight above the threshold

	// A 25-weight runner-up shrinks the lead to 5, below the threshold of 10, cancelling quorum
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (25), nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	auto round2 = ballot.evaluate (10);
	ASSERT_EQ (30, round2.winner_weight);
	ASSERT_EQ (55, total_weight (ballot));
	ASSERT_FALSE (round2.quorum);
}

/*
 * Two forks with exactly equal weight remain distinct tally entries and their tie permanently blocks quorum, since neither leads the other.
 * The winner slot still resolves deterministically to the higher hash, mirroring the vote tie-break rule, so all nodes agree on which fork they are backing while the tie lasts.
 */
TEST (election_ballot, evaluate_equal_forks_block_quorum)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// 20 vs 20: the total of 40 passes the gate, but the lead between the forks is zero
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (20), nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (20), nano::vote::timestamp_min, fork->hash (), 0s, epoch));

	// Both forks stay distinct in the tally and a tie can never reach quorum
	auto round = ballot.evaluate (10);
	auto tally = ballot.tally ();
	ASSERT_EQ (2, tally.size ());
	ASSERT_EQ (40, total_weight (ballot));
	ASSERT_EQ (20, round.winner_weight);
	ASSERT_FALSE (round.quorum);

	// The heavier hash leads the tally on equal weights
	auto const leader = std::max (initial->hash (), fork->hash ());
	ASSERT_EQ (leader, tally.begin ()->first.hash);
	ASSERT_EQ (leader, round.winner->hash ());
}

/*
 * Final quorum is reached only by final-vote weight: normal votes count towards regular quorum but never towards final quorum, no matter how much weight they carry.
 * Regular quorum triggering the node's own final vote and final quorum confirming the election are separate thresholds, and this test pins that separation.
 */
TEST (election_ballot, evaluate_final_quorum)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };

	// 8 normal plus 7 final weight back the winner
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (8), nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (7), nano::vote::timestamp_final, initial->hash (), 0s, epoch));

	// Winner weight 15 reaches quorum, but the final weight of 7 alone misses the threshold of 10
	auto round1 = ballot.evaluate (10);
	ASSERT_EQ (15, round1.winner_weight);
	ASSERT_EQ (7, round1.final_winner_weight);
	ASSERT_TRUE (round1.quorum);
	ASSERT_FALSE (round1.final_quorum);

	// 5 more final weight brings the final total to 12, above the threshold
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (5), nano::vote::timestamp_final, initial->hash (), 0s, epoch));
	auto round2 = ballot.evaluate (10);
	ASSERT_EQ (12, round2.final_winner_weight);
	ASSERT_TRUE (round2.final_quorum);
}

/*
 * The reported final weight is the final-vote weight behind the current winner, not behind any block.
 * Final votes cast for a losing fork must not count towards confirming the winner, or an election could confirm a block the finalizing reps never committed to.
 */
TEST (election_ballot, evaluate_final_weight_follows_winner)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// 20 normal weight keeps the initial block winning while 5 final weight sits on the losing fork
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (20), nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (5), nano::vote::timestamp_final, fork->hash (), 0s, epoch));

	// Final votes behind a losing fork do not count towards the winner's final weight
	auto round = ballot.evaluate (10);
	ASSERT_EQ (initial, round.winner);
	ASSERT_EQ (20, round.winner_weight);
	ASSERT_EQ (0, round.final_winner_weight);
	ASSERT_FALSE (round.final_quorum);
}

/*
 * The tally query is a pure report: it shows the current standings without advancing the winner.
 * Only evaluate may move the winner, so status displays and RPC endpoints can inspect the tally freely without side effects on consensus state.
 */
TEST (election_ballot, tally_does_not_advance_winner)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// The fork gathers enough weight to win, but no evaluation has run yet
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (reps.rep (30), nano::vote::timestamp_min, fork->hash (), 0s, epoch));

	// The tally query reports the fork in the lead, but only evaluate may move the winner
	auto tally = ballot.tally ();
	ASSERT_EQ (1, tally.size ());
	ASSERT_EQ (fork->hash (), tally.begin ()->first.hash);
	ASSERT_EQ (initial, ballot.winner ());

	// The pending switch happens on the next evaluation
	ASSERT_EQ (fork, ballot.evaluate (10).winner);
	ASSERT_EQ (fork, ballot.winner ());
}

/*
 * The votes_with_weight report lists every recorded vote joined with the rep's current weight, ordered heaviest rep first.
 * Reps with equal weight are ordered by account, so the report is fully deterministic.
 * This is the presentation used by RPC to show who is backing what.
 */
TEST (election_ballot, votes_with_weight_ordering)
{
	test_reps reps;
	auto initial = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };

	// Weights 1, 10, 5 and 5 registered and voted in shuffled order
	auto rep_small = reps.rep (1);
	auto rep_large = reps.rep (10);
	auto rep_medium = reps.rep (5);
	auto rep_equal1 = reps.rep (5);

	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep_small, nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep_large, nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep_medium, nano::vote::timestamp_min, initial->hash (), 0s, epoch));
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep_equal1, nano::vote::timestamp_min, initial->hash (), 0s, epoch));

	// Expected order: weight 10 first, the two 5-weight reps by account, weight 1 last
	auto votes = ballot.votes_with_weight ();
	ASSERT_EQ (4, votes.size ());
	ASSERT_EQ (rep_large, votes[0].representative);
	ASSERT_EQ (10, votes[0].weight);

	// The two equal-weight reps are ordered by ascending account
	ASSERT_EQ (std::min (rep_medium, rep_equal1), votes[1].representative);
	ASSERT_EQ (std::max (rep_medium, rep_equal1), votes[2].representative);
	ASSERT_EQ (rep_small, votes[3].representative);
	ASSERT_EQ (initial->hash (), votes[0].hash);
}

/*
 * The block and vote queries expose the held state faithfully: blocks () returns every held block by hash, find/contains resolve individual hashes, and votes () returns the recorded votes by representative account.
 */
TEST (election_ballot, block_queries)
{
	test_reps reps;
	auto initial = create_block ();
	auto fork = create_block ();
	nano::election_ballot ballot{ initial, reps.query () };
	ASSERT_EQ (nano::election_ballot::insert_outcome::inserted, ballot.insert (fork).outcome);

	// Both held blocks are visible through every block query
	ASSERT_EQ (2, ballot.block_count ());
	auto blocks = ballot.blocks ();
	ASSERT_EQ (2, blocks.size ());
	ASSERT_EQ (initial, blocks.at (initial->hash ()));
	ASSERT_EQ (fork, blocks.at (fork->hash ()));
	ASSERT_TRUE (ballot.contains_block (fork->hash ()));
	ASSERT_EQ (fork, ballot.find_block (fork->hash ()));

	// A recorded vote is visible through the votes query under the rep's account
	auto rep = reps.rep (3);
	ASSERT_EQ (nano::election_ballot::vote_result::accepted, ballot.vote (rep, nano::vote::timestamp_min, fork->hash (), 0s, epoch));
	auto votes = ballot.votes ();
	ASSERT_EQ (1, votes.size ());
	ASSERT_EQ (fork->hash (), votes.at (rep).hash);
}
