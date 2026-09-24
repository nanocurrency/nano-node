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

/*
 * Block 3 of the ledger-bloat mitigation work: the retention planner. A storage-capped
 * node keeps, per account, only its cemented frontier plus a bounded window of recent
 * history, and discards everything cemented below that - the discarded content stays
 * committed under the checkpoint root and can be re-fetched with a proof if ever needed.
 *
 * This layer computes the SAFE-TO-DROP set and proves it is safe (every retained account
 * still verifies against the checkpoint root) and quantifies the reclaimable storage. It
 * does NOT delete anything - destructive pruning + on-demand backfill is a follow-on that
 * builds on this planner as its safety gate.
 */

// An anchor a storage-capped node (and light clients) prove against: the commitment root
// at a given cemented height, with the leaf counts it binds.
struct state_checkpoint final
{
	uint64_t cemented_height{ 0 }; // ledger.cemented_count() at capture time
	nano::block_hash root{ 0 };
	uint64_t account_count{ 0 };
	uint64_t pending_count{ 0 };
};

// Capture the current cemented commitment as a checkpoint.
nano::state_checkpoint capture_state_checkpoint (nano::ledger const &, nano::secure::transaction const &);

// The result of planning capped retention against a freshly captured checkpoint.
struct capped_retention_plan final
{
	nano::state_checkpoint checkpoint;
	uint64_t history_window{ 0 }; // blocks kept per account, counting the frontier
	uint64_t kept_blocks{ 0 }; // blocks a capped node would retain (frontier + window)
	uint64_t droppable_blocks{ 0 }; // cemented blocks below the window, safe to drop
	uint64_t retained_pending{ 0 }; // cemented pending entries (kept until Block 4)
	uint64_t reclaimable_bytes{ 0 }; // droppable_blocks * approximate stored block size
	uint64_t accounts_checked{ 0 }; // accounts whose frontier proof was verified
	uint64_t accounts_proven{ 0 }; // of those, how many verified against checkpoint.root
	bool all_proven{ false }; // accounts_proven == accounts_checked (no failures)
};

// Plan capped retention: classify cemented blocks as kept vs droppable for the given
// per-account history window, quantify reclaimable bytes, and verify frontier proofs for
// up to `max_accounts_to_prove` accounts against the checkpoint root (0 = prove all).
// Proving is O(accounts_checked * total accounts), so callers off a bound it for large
// ledgers; accounts_checked vs checkpoint.account_count reports any sampling.
nano::capped_retention_plan plan_capped_retention (nano::ledger const &, nano::secure::transaction const &, uint64_t history_window, uint64_t max_accounts_to_prove);

/*
 * Block 4 of the ledger-bloat mitigation work: the cold-pending sweep. The pending
 * table is the un-prunable dust residue - a send to a never-opened account mints a
 * receivable that lives forever on every node. We do NOT expire or return it (that
 * would break the irreversible-send guarantee and is unsound without a global clock).
 * Instead we identify AGED, SUB-THRESHOLD pending entries and move them out of the hot
 * working set into a committed COLD subtree. A cold entry is never lost: its membership
 * stays committed under `cold_root`, and its receiver can still claim it at any time by
 * presenting an inclusion proof. This layer classifies and proves; it deletes nothing.
 */

// A pending entry qualifies as cold when it is at least `min_age_seconds` old (by the
// send block's timestamp vs a caller-supplied reference time) AND its amount is at or
// below `amount_threshold` (dust). The caller supplies the reference time so the library
// stays deterministic and testable.
struct pending_sweep_plan final
{
	nano::state_checkpoint checkpoint;
	uint64_t reference_timestamp{ 0 };
	uint64_t min_age_seconds{ 0 };
	nano::amount amount_threshold{ 0 };

	uint64_t hot_count{ 0 }; // pending entries a node keeps resident
	uint64_t cold_count{ 0 }; // aged sub-threshold entries offloadable to the cold subtree
	nano::block_hash cold_root{ 0 }; // MMR root committing exactly the cold entries
	uint64_t reclaimable_pending_bytes{ 0 }; // cold_count * approximate pending record size

	uint64_t cold_checked{ 0 }; // cold entries whose claimability proof was verified
	uint64_t cold_proven{ 0 }; // of those, how many verified against cold_root
	bool all_proven{ false };
};

nano::pending_sweep_plan plan_pending_sweep (nano::ledger const &, nano::secure::transaction const &, uint64_t reference_timestamp, uint64_t min_age_seconds, nano::amount const & amount_threshold, uint64_t max_to_prove);

// A standalone proof that a cold pending entry is committed under `cold_root`, so its
// receiver can claim it after it has been offloaded from the hot table. Verification is
// pure (no store access).
struct cold_pending_proof final
{
	nano::state_proof_pending_claim claim;
	std::vector<nano::state_proof_step> path;
	std::vector<nano::block_hash> peaks;
	uint64_t peak_index{ 0 };
	nano::block_hash cold_root{ 0 };
};

std::optional<nano::cold_pending_proof> generate_cold_pending_proof (nano::ledger const &, nano::secure::transaction const &, uint64_t reference_timestamp, uint64_t min_age_seconds, nano::amount const & amount_threshold, nano::pending_key const &);
bool verify_cold_pending_proof (nano::cold_pending_proof const &);
}
