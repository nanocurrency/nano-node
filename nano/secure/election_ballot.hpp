#pragma once

#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/numbers_templ.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nano
{
// A representative's most recent vote observed by one election
struct vote_info final
{
	std::chrono::steady_clock::time_point arrival{}; // When this node received the vote, anchors the cooldown window
	uint64_t timestamp{ 0 }; // Timestamp carried by the vote itself, higher supersedes lower
	nano::block_hash hash{ 0 }; // Block the representative voted for

	// Whether the timestamp marks a final vote
	bool final () const;
};

// A recorded vote joined with the representative's current weight
struct vote_with_weight_info final
{
	nano::account representative{ 0 }; // Representative that cast the vote
	std::chrono::steady_clock::time_point arrival{}; // When this node received the vote
	uint64_t timestamp{ 0 }; // Timestamp carried by the vote
	nano::block_hash hash{ 0 }; // Block the representative voted for
	nano::uint128_t weight{ 0 }; // Representative weight when the report was created
};

// Tally position of one block
struct tally_key final
{
	nano::uint128_t weight;
	nano::block_hash hash;
};

// Orders tally entries heaviest first, equal weights by higher hash, mirroring the vote tie-break rule
struct tally_key_greater final
{
	bool operator() (nano::tally_key const &, nano::tally_key const &) const;
};

// Blocks ordered by tally position; hashes are unique, so equal-weight forks remain distinct entries
using tally_map = std::map<nano::tally_key, std::shared_ptr<nano::block>, nano::tally_key_greater>;

// Minimum time between subsequent non-final votes from a representative of the given weight
std::chrono::seconds calculate_vote_cooldown (nano::uint128_t weight, nano::uint128_t online_stake);

/**
 * Consensus bookkeeping for a single election: records one vote per representative, holds the competing blocks (forks of a single root) and owns the current winner, advancing it by the quorum rule.
 * The entire state is { votes, blocks, winner }, each mutator writes exactly one of them and every tally is computed fresh from the recorded votes.
 * Representative weight and time are supplied by the caller, making the class deterministic and self-contained; locking is the responsibility of the owning election.
 *
 * Invariants:
 * - One vote per representative, so a rep can never back two blocks at once.
 * - A rep's vote only advances in (timestamp, hash) order and is never erased, so a replayed older vote can never overwrite a newer one, regardless of which blocks come and go.
 * - The number of held blocks never exceeds the maximum.
 * - The winner is always one of the held blocks and is never evicted.
 */
class election_ballot final
{
public:
	static size_t constexpr default_max_blocks{ 10 };

	using weight_fn = std::function<nano::uint128_t (nano::account const &)>;

	// The initial block becomes the first winner
	election_ballot (std::shared_ptr<nano::block> const & initial, weight_fn weight_query, size_t max_blocks = default_max_blocks);

public: // Votes
	enum class vote_result
	{
		accepted, // Vote recorded as the rep's current vote
		replay, // Does not rank above the rep's recorded vote, ordered by timestamp then hash
		ignored, // Valid but arrived within the rep's cooldown window
	};

	// Record a representative's vote
	// The hash may refer to a block this ballot does not hold: the vote is still recorded and anchors the rep's replay and cooldown checks, but carries no tally weight while unheld
	// A cooldown of 0 disables throttling; a rep's first final vote always bypasses it
	vote_result vote (nano::account const & rep, uint64_t timestamp, nano::block_hash const & hash, std::chrono::seconds cooldown, std::chrono::steady_clock::time_point now);

	// The representative's recorded vote, if any
	std::optional<nano::vote_info> find_vote (nano::account const &) const;

public: // Blocks
	enum class insert_outcome
	{
		inserted, // Newly added
		updated, // Hash already present, the stored block was refreshed (e.g. with higher work)
		replaced, // Ballot was full, the weakest non-winner block was evicted to make room
		rejected, // Ballot is full and the block's backing weight does not outweigh any non-winner block
	};

	struct insert_result final
	{
		insert_outcome outcome;
		std::shared_ptr<nano::block> evicted{}; // Set when replaced, so the caller can undo routing and filters
	};

	// The only way blocks enter or leave, keeping the ballot within its block limit
	// `cached_tally` is externally observed weight backing the incoming block (e.g. from the vote cache); when the ballot is full, the stronger of it and the block's retained tallied weight must exceed the weakest non-winner's tallied weight to evict it
	// Eviction does not touch recorded votes: votes for an evicted block keep anchoring their reps, back its readmission and count again once it is re-inserted
	insert_result insert (std::shared_ptr<nano::block> const &, nano::uint128_t cached_tally = 0);

public: // Tally
	struct round final
	{
		std::shared_ptr<nano::block> winner; // Current winner after this round, never null
		nano::uint128_t winner_weight{ 0 }; // Vote weight behind the winner, normal + final votes
		nano::uint128_t final_winner_weight{ 0 }; // Vote weight behind the winner, final votes only
		bool quorum{ false }; // The winner leads every rival by the full threshold: safe to issue a final vote for it
		bool final_quorum{ false }; // A full threshold of weight has committed to the winner with final votes: safe to confirm the election
	};

	// Recompute the tally and advance the winner; the only state change besides vote/insert
	// The heaviest block takes the winner slot only once `total_weight` reaches `quorum_threshold`, so a lead among the first few votes cannot move the winner while participation is still low
	// Votes for unheld blocks count neither toward the tally nor toward `total_weight`: they express no preference between the held blocks, and counting them would let weight that cannot back any candidate push the winner around
	[[nodiscard]] round evaluate (nano::uint128_t quorum_threshold);

public: // Queries
	// Current winner, never null
	std::shared_ptr<nano::block> winner () const;

	// The held block with the given hash, or null
	std::shared_ptr<nano::block> find_block (nano::block_hash const &) const;

	// Whether a block with the given hash is held
	bool contains_block (nano::block_hash const &) const;

	// Current tally without advancing the winner
	nano::tally_map tally () const;

	// All held blocks by hash
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks () const;

	// All recorded votes by representative
	std::unordered_map<nano::account, nano::vote_info> votes () const;

	// All recorded votes with the reps' current weights, heaviest rep first
	std::vector<nano::vote_with_weight_info> votes_with_weight () const;

	// Number of representatives with a recorded vote
	size_t voter_count () const;

	// Number of held blocks
	size_t block_count () const;

private:
	// Vote weight per voted-for block hash, including unheld hashes
	struct block_weights final
	{
		std::unordered_map<nano::block_hash, nano::uint128_t> weights; // Normal + final votes
		std::unordered_map<nano::block_hash, nano::uint128_t> final_weights; // Final votes only

		// Weight behind the hash (normal + final votes), zero when nothing is tallied for it
		nano::uint128_t weight (nano::block_hash const &) const;

		// Weight behind the hash from final votes only, zero when nothing is tallied for it
		nano::uint128_t final_weight (nano::block_hash const &) const;
	};

	block_weights compute_weights () const;

	// Order the weights of held blocks into a tally, dropping weight behind unheld hashes
	nano::tally_map make_tally (std::unordered_map<nano::block_hash, nano::uint128_t> const & weights) const;

private: // Dependencies
	weight_fn const weight_query;
	size_t const max_blocks;

private: // The entire mutable state, one field per mutator
	std::unordered_map<nano::account, nano::vote_info> votes_m; // vote (): latest vote per rep, may reference unheld hashes
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks_m; // insert (): competing blocks, always includes the winner
	nano::block_hash winner_m; // evaluate (): current winner, always one of the held blocks
};
}
