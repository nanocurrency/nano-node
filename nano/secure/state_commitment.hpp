#pragma once

#include <nano/lib/numbers.hpp>

#include <cstdint>

namespace nano
{
class ledger;
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
}
