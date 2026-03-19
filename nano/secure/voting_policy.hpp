#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/secure/fwd.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace nano
{
class vote;

enum class vote_type
{
	normal,
	final
};

/**
 * Proof that a vote eligibility check was performed.
 * Can only be created by voting_policy — private constructor ensures callers cannot bypass the policy.
 */
class vote_permit
{
	friend class voting_policy;
	vote_permit (nano::qualified_root qualified_root, nano::block_hash hash, nano::vote_type type);

	nano::qualified_root qualified_root_m;
	nano::block_hash hash_m;
	nano::vote_type type_m;

public:
	nano::qualified_root const & qualified_root () const;
	nano::root root () const;
	nano::block_hash const & hash () const;
	nano::vote_type type () const;
};

/**
 * Centralizes vote eligibility decisions and signing.
 * Single source of truth for whether a block should be voted on.
 * Vote construction must only be handled here.
 */
class voting_policy
{
public:
	explicit voting_policy (nano::ledger &);

	/**
	 * Check normal vote eligibility for an already-fetched block.
	 * Returns a permit if the block's dependencies are cemented and no conflicting final vote exists.
	 */
	std::optional<vote_permit> vote_normal (
	nano::secure::transaction const &,
	nano::block const & block) const;

	/**
	 * Check final vote eligibility for an already-fetched block.
	 * Returns a permit if dependencies are cemented and the final vote slot can be claimed (final_vote.put).
	 * Requires a write transaction because it persists the final vote decision.
	 */
	std::optional<vote_permit> vote_final (
	nano::secure::write_transaction const &,
	nano::block const & block) const;

	/**
	 * Read-only final vote eligibility for the reply path.
	 * If a final vote was previously recorded for this root, returns a permit for the *recorded* hash
	 * (which may differ from the queried block's hash in the case of forks).
	 * Falls back to allowing the block's own hash if the block is cemented.
	 * Does not persist any state.
	 */
	std::optional<vote_permit> reply_final (
	nano::secure::transaction const &,
	nano::block const & block) const;

	/**
	 * Signer iterates representative key pairs and calls the provided callback for each.
	 */
	using vote_signer_t = std::function<void (
	std::function<void (nano::public_key const &, nano::raw_key const &)> const &)>;

	/**
	 * Sign votes for the given permits.
	 * All permit hashes are batched into a single vote per representative.
	 * The signer provides representative key pairs via callback.
	 * Timestamp and duration are controlled based on vote type.
	 */
	std::vector<std::shared_ptr<nano::vote>> sign (
	nano::vote_type type,
	std::vector<vote_permit> const & permits,
	vote_signer_t const & signer,
	nano::millis_t timestamp = nano::milliseconds_since_epoch ()) const;

private:
	nano::ledger & ledger;
};
}
