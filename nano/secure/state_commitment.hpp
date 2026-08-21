#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace nano
{
class ledger;
class pending_key;
namespace secure
{
	class transaction;
}

/*
 * Block 1 of the ledger-bloat mitigation work: a deterministic Merkle commitment
 * over the cemented ledger state. This is a read-only measurement primitive - it
 * does not change consensus, block validation, or storage. Its purpose is to prove
 * that a single canonical root can be derived from the cemented account frontier and
 * the cemented pending set, so that later blocks (proof service, storage-capped nodes)
 * can let a node discard history / cold-store dust while still proving any account's
 * balance against this root.
 *
 * The root is a PURE FUNCTION of cemented state: two honest nodes that have cemented
 * the same set of blocks must produce byte-identical roots regardless of sync order.
 */
struct state_commitment_result final
{
	// Overall canonical root binding both sub-roots and their counts.
	nano::block_hash root{ 0 };
	// Merkle root over the cemented account frontier leaves.
	nano::block_hash accounts_root{ 0 };
	// Merkle root over the cemented pending (receivable) leaves.
	nano::block_hash pending_root{ 0 };
	// Number of cemented accounts folded into accounts_root.
	uint64_t account_count{ 0 };
	// Number of cemented pending entries folded into pending_root.
	uint64_t pending_count{ 0 };
};

/*
 * Walk the cemented account frontier (confirmation_height table) and the cemented
 * pending set (pending table, restricted to entries whose send block is cemented),
 * hashing each into a leaf and folding the leaves - in canonical store key order -
 * into a Merkle Mountain Range accumulator. Returns the resulting roots and counts.
 *
 * Cost is O(cemented accounts + cemented pending) store reads and O(log n) memory.
 * Intended to be invoked off the hot path (e.g. via the state_commitment RPC).
 */
nano::state_commitment_result compute_state_commitment (nano::ledger const &, nano::secure::transaction const &);

/*
 * Block 2 of the ledger-bloat mitigation work: a succinct inclusion proof against
 * the commitment root produced by compute_state_commitment(). A holder of only the
 * trusted root (a light / storage-capped node) can verify that a specific account
 * frontier+balance, or a specific pending entry, is part of cemented state - without
 * holding the ledger. Verification is a PURE function of the proof and touches no
 * store, so it can run anywhere.
 */

// One authentication step from a leaf up to its Merkle Mountain Range peak.
struct state_proof_step final
{
	nano::block_hash hash{ 0 };
	// True if `hash` is the right-hand sibling (so the running value is on the left).
	bool sibling_on_right{ false };
};

// The account-frontier claim an account proof attests to.
struct state_proof_account_claim final
{
	nano::account account{};
	nano::block_hash frontier{ 0 };
	nano::amount balance{ 0 };
	uint64_t height{ 0 };
};

// The pending-entry claim a pending proof attests to.
struct state_proof_pending_claim final
{
	nano::account account{}; // receiving (destination) account
	nano::block_hash hash{ 0 }; // send block hash
	nano::account source{};
	nano::amount amount{ 0 };
	nano::epoch epoch{ nano::epoch::epoch_0 };
};

struct state_proof final
{
	// Exactly one of these is populated; it identifies the sub-tree and the claim.
	std::optional<state_proof_account_claim> account_claim;
	std::optional<state_proof_pending_claim> pending_claim;

	// Authentication path from the claimed leaf up to its mountain peak.
	std::vector<state_proof_step> path;
	// All mountain peaks in ascending-height (bag consumption) order.
	std::vector<nano::block_hash> peaks;
	// Index into `peaks` of the peak this claim's mountain reaches.
	uint64_t peak_index{ 0 };

	// Root of the OTHER sub-tree (pending_root for an account proof, and vice versa),
	// needed to rebuild the overall root.
	nano::block_hash other_root{ 0 };
	uint64_t account_count{ 0 };
	uint64_t pending_count{ 0 };

	// The overall commitment root this proof reconstructs to.
	nano::block_hash root{ 0 };
};

// Build an inclusion proof for `account`'s cemented frontier. Returns nullopt if the
// account is not in the cemented frontier (or its frontier block is unresolvable).
std::optional<nano::state_proof> generate_account_proof (nano::ledger const &, nano::secure::transaction const &, nano::account const &);

// Build an inclusion proof for a cemented pending entry keyed by (account, send hash).
std::optional<nano::state_proof> generate_pending_proof (nano::ledger const &, nano::secure::transaction const &, nano::pending_key const &);

// Verify a proof reconstructs its stated root from its claim. Pure; no store access.
bool verify_state_proof (nano::state_proof const &);
}
