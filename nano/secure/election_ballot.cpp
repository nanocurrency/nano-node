#include <nano/lib/assert.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/vote.hpp>
#include <nano/secure/election_ballot.hpp>
#include <nano/secure/rep_tiers.hpp>

#include <algorithm>

/*
 * vote_info
 */

bool nano::vote_info::final () const
{
	return nano::vote::is_final_timestamp (timestamp);
}

/*
 * tally_key
 */

bool nano::tally_key_greater::operator() (nano::tally_key const & lhs, nano::tally_key const & rhs) const
{
	if (lhs.weight != rhs.weight)
	{
		return lhs.weight > rhs.weight;
	}
	return lhs.hash > rhs.hash;
}

/*
 * calculate_vote_cooldown
 */

std::chrono::seconds nano::calculate_vote_cooldown (nano::uint128_t weight, nano::uint128_t online_stake)
{
	// The throttling levels follow the shared rep tier boundaries
	auto tier = nano::calculate_rep_tier (weight, online_stake);
	switch (tier)
	{
		case nano::rep_tier::tier_3:
			return std::chrono::seconds{ 1 };
		case nano::rep_tier::tier_2:
			return std::chrono::seconds{ 5 };
		case nano::rep_tier::tier_1:
		case nano::rep_tier::none:
			return std::chrono::seconds{ 15 };
	}
	debug_assert (false);
	return std::chrono::seconds{ 15 };
}

/*
 * election_ballot
 */

nano::election_ballot::election_ballot (std::shared_ptr<nano::block> const & initial, weight_fn weight_query_a, size_t max_blocks_a) :
	weight_query{ std::move (weight_query_a) },
	max_blocks{ max_blocks_a },
	winner_m{ initial->hash () }
{
	debug_assert (initial != nullptr);
	debug_assert (max_blocks > 0);

	// The initial block seeds both invariants: the winner always exists and is always held
	blocks_m.emplace (initial->hash (), initial);
}

/*
 * Votes
 */

nano::election_ballot::vote_result nano::election_ballot::vote (nano::account const & rep, uint64_t timestamp, nano::block_hash const & hash, std::chrono::seconds cooldown, std::chrono::steady_clock::time_point now)
{
	// A rep's first vote is admitted unconditionally; later votes are checked against the recorded one
	if (auto existing = votes_m.find (rep); existing != votes_m.end ())
	{
		auto const & last_vote = existing->second;

		// Replay checks come before throttling, so a stale vote is rejected even inside the cooldown window
		if (last_vote.timestamp > timestamp)
		{
			return vote_result::replay;
		}

		// Equal timestamps only advance towards the higher hash, keeping the rep's vote sequence a strict total order
		if (last_vote.timestamp == timestamp && !(last_vote.hash < hash))
		{
			return vote_result::replay;
		}

		// A rep's first final vote passes immediately, a final vote replacing another final does not
		bool const first_final = nano::vote::is_final_timestamp (timestamp) && !last_vote.final ();
		bool const past_cooldown = last_vote.arrival <= now - cooldown;
		if (!first_final && !past_cooldown)
		{
			return vote_result::ignored;
		}
	}

	// The vote replaces the rep's recorded vote entirely and becomes the anchor for future admission checks
	votes_m[rep] = { now, timestamp, hash };
	return vote_result::accepted;
}

std::optional<nano::vote_info> nano::election_ballot::find_vote (nano::account const & rep) const
{
	if (auto existing = votes_m.find (rep); existing != votes_m.end ())
	{
		return existing->second;
	}
	return std::nullopt;
}

bool nano::election_ballot::has_vote_for (nano::block_hash const & hash) const
{
	return std::any_of (votes_m.begin (), votes_m.end (), [&hash] (auto const & entry) {
		return entry.second.hash == hash;
	});
}

/*
 * Blocks
 */

auto nano::election_ballot::insert (std::shared_ptr<nano::block> const & block, nano::uint128_t cached_tally) -> insert_result
{
	debug_assert (block != nullptr);

	auto const hash = block->hash ();

	// A block already held is refreshed in place, e.g. the same block republished with a higher work value
	if (auto existing = blocks_m.find (hash); existing != blocks_m.end ())
	{
		existing->second = block;
		return { insert_outcome::updated };
	}

	if (blocks_m.size () < max_blocks)
	{
		blocks_m.emplace (hash, block);
		return { insert_outcome::inserted };
	}

	// Full: find the weakest non-winner block by the live tally, computed fresh so no stale snapshot can misrank the candidates
	auto const block_weights = compute_weights ();
	std::optional<std::pair<nano::uint128_t, nano::block_hash>> weakest;

	for (auto const & [held_hash, held_block] : blocks_m)
	{
		// The winner is never an eviction candidate, regardless of its weight
		if (held_hash == winner_m)
		{
			continue;
		}

		// Blocks nobody voted for weigh nothing and are evicted first
		nano::uint128_t const held_weight = block_weights.weight (held_hash);

		// Ties are broken by lower hash, so every node holding the same state evicts the same block
		std::pair<nano::uint128_t, nano::block_hash> const entry{ held_weight, held_hash };

		if (!weakest || entry < *weakest)
		{
			weakest = entry;
		}
	}
	// The incoming block is also backed by any votes retained for it here; a rep may be counted by both sources, so they must not add
	auto const incoming_weight = std::max (cached_tally, block_weights.weight (hash));

	// Evict only if the incoming block is backed by strictly more weight than the weakest, equal weight favors the incumbent
	if (!weakest || incoming_weight <= weakest->first)
	{
		return { insert_outcome::rejected };
	}

	// Only the block is removed, its recorded votes are deliberately retained (see the class description)
	auto evicted = blocks_m.find (weakest->second)->second;
	blocks_m.erase (weakest->second);
	blocks_m.emplace (hash, block);

	debug_assert (blocks_m.size () <= max_blocks);

	return { insert_outcome::replaced, evicted };
}

/*
 * Tally
 */

nano::uint128_t nano::election_ballot::block_weights::weight (nano::block_hash const & hash) const
{
	auto existing = weights.find (hash);
	return existing != weights.end () ? existing->second : 0;
}

nano::uint128_t nano::election_ballot::block_weights::final_weight (nano::block_hash const & hash) const
{
	auto existing = final_weights.find (hash);
	return existing != final_weights.end () ? existing->second : 0;
}

auto nano::election_ballot::compute_weights () const -> block_weights
{
	// Accumulate the weight behind every voted-for hash, including hashes not held; make_tally filters those out
	block_weights result;
	for (auto const & [rep, info] : votes_m)
	{
		auto const rep_weight = weight_query (rep);
		result.weights[info.hash] += rep_weight;
		// A final vote counts into both totals, so the final weight is always a subset of the block weight
		if (info.final ())
		{
			result.final_weights[info.hash] += rep_weight;
		}
	}
	return result;
}

nano::tally_map nano::election_ballot::make_tally (std::unordered_map<nano::block_hash, nano::uint128_t> const & weights) const
{
	// Only held blocks enter the tally, weight behind unheld hashes is dropped here; the map comparator orders the entries
	nano::tally_map result;
	for (auto const & [hash, weight] : weights)
	{
		if (auto held = blocks_m.find (hash); held != blocks_m.end ())
		{
			result.emplace (nano::tally_key{ weight, hash }, held->second);
		}
	}
	return result;
}

auto nano::election_ballot::evaluate (nano::uint128_t quorum_threshold) -> round
{
	auto const block_weights = compute_weights ();

	auto const tally = make_tally (block_weights.weights);
	// Participation is the weight distributed over held blocks, votes for unheld hashes were filtered out of the tally
	nano::uint128_t total_weight{ 0 };
	for (auto const & [key, block] : tally)
	{
		total_weight += key.weight;
	}

	round result;
	// The heaviest block takes over as winner only once enough weight participates in the tally
	if (!tally.empty () && total_weight >= quorum_threshold)
	{
		winner_m = tally.begin ()->first.hash;
	}

	// The winner may have no votes at all, e.g. the initial block before any vote arrived, so both weights may be zero
	result.winner = winner ();
	result.winner_weight = block_weights.weight (winner_m);
	result.final_winner_weight = block_weights.final_weight (winner_m);

	// Quorum requires the winner to lead the runner-up by the full threshold, not merely reach it in absolute weight, so a close race between heavy forks does not count as a decided election
	nano::uint128_t runner_up{ 0 };
	for (auto const & [key, block] : tally)
	{
		if (key.hash != winner_m)
		{
			runner_up = key.weight; // The tally is ordered, so the first non-winner entry is the heaviest
			break;
		}
	}
	// While the participation gate holds the tally leader out, the winner can trail the runner-up; the first comparison rejects that case and guards the unsigned subtraction
	result.quorum = result.winner_weight >= runner_up && result.winner_weight - runner_up >= quorum_threshold;
	result.final_quorum = result.final_winner_weight >= quorum_threshold;

	return result;
}

/*
 * Queries
 */

std::shared_ptr<nano::block> nano::election_ballot::winner () const
{
	// The winner hash always resolves to a held block: insert never evicts it and evaluate only assigns held hashes
	auto existing = blocks_m.find (winner_m);
	release_assert (existing != blocks_m.end ());
	return existing->second;
}

std::shared_ptr<nano::block> nano::election_ballot::find_block (nano::block_hash const & hash) const
{
	if (auto existing = blocks_m.find (hash); existing != blocks_m.end ())
	{
		return existing->second;
	}
	return nullptr;
}

bool nano::election_ballot::contains_block (nano::block_hash const & hash) const
{
	return blocks_m.contains (hash);
}

nano::tally_map nano::election_ballot::tally () const
{
	return make_tally (compute_weights ().weights);
}

std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> nano::election_ballot::blocks () const
{
	return blocks_m;
}

std::unordered_map<nano::account, nano::vote_info> nano::election_ballot::votes () const
{
	return votes_m;
}

std::vector<nano::vote_with_weight_info> nano::election_ballot::votes_with_weight () const
{
	std::vector<nano::vote_with_weight_info> result;
	result.reserve (votes_m.size ());
	for (auto const & [rep, info] : votes_m)
	{
		result.push_back ({ rep, info.arrival, info.timestamp, info.hash, weight_query (rep) });
	}

	// Heaviest reps first, ties ordered by account so the report is deterministic
	std::sort (result.begin (), result.end (), [] (auto const & lhs, auto const & rhs) {
		if (lhs.weight != rhs.weight)
		{
			return lhs.weight > rhs.weight;
		}
		return lhs.representative < rhs.representative;
	});

	return result;
}

size_t nano::election_ballot::voter_count () const
{
	return votes_m.size ();
}

size_t nano::election_ballot::block_count () const
{
	return blocks_m.size ();
}
