#pragma once

#include <nano/lib/numbers.hpp>

#include <cstdint>

namespace nano
{
class block;
class ledger;
namespace secure
{
	class transaction;
}

/*
 * Block 5 of the ledger-bloat mitigation work: the storage-weighted proof-of-work
 * throttle. Proof-of-work today is a one-time issuance cost uncorrelated with the
 * PERMANENT storage a block imposes, so minting dust is nearly free. This prices the
 * mass-dust vector in CPU (never a monetary fee, so the feeless model is preserved):
 * a state send whose destination account is not yet opened creates new permanent
 * ledger footprint (a fresh account / receivable that may never be received), so it is
 * required to carry more work than a send to an already-existing account.
 *
 * Legitimate first-contact onboarding pays the higher cost once per real new account -
 * rare and acceptable - while an attacker fanning dust out to fabricated destinations
 * pays it on every block. An attacker can dodge by pre-opening the destinations they
 * control, but that itself costs an open + receive per destination, which is the
 * self-limiting cost the throttle is meant to impose.
 *
 * This is an ADVISORY policy calculator: it computes the storage-weighted requirement
 * and whether a block meets it. It does not change consensus work validation, which
 * would require network-wide activation.
 */

// True if the block adds a brand-new account to permanent state: a send whose
// destination account is not yet opened in the ledger.
bool block_adds_new_account (nano::ledger const &, nano::secure::transaction const &, nano::block const &);

struct storage_weighted_work_result final
{
	bool creates_new_account{ false }; // whether the storage weight applies
	double weight_multiplier{ 1.0 }; // multiplier applied to the base requirement
	uint64_t base_threshold{ 0 }; // normal work requirement for this block
	uint64_t required_threshold{ 0 }; // storage-weighted requirement (>= base_threshold)
	uint64_t achieved_difficulty{ 0 }; // difficulty of the block's current work
	bool satisfies{ false }; // achieved_difficulty >= required_threshold
};

// Evaluate a block against the storage-weighted requirement. `new_account_multiplier`
// (>= 1.0) is how much harder a state-creating send must work. Advisory / measurement.
nano::storage_weighted_work_result evaluate_storage_weighted_work (nano::ledger const &, nano::secure::transaction const &, nano::block const &, double new_account_multiplier);
}
