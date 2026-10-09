#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/storage_weighted_work.hpp>
#include <nano/secure/transaction.hpp>

#include <algorithm>

namespace
{
/*
 * Whether `block` is a send. A block that has not been processed yet (the usual case
 * when pricing work for it) has no sideband, and block::is_send () asserts on that,
 * so a state block's direction is read from its previous balance instead.
 */
bool is_send_unprocessed (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::block const & block)
{
	if (block.has_sideband ())
	{
		return block.is_send ();
	}
	switch (block.type ())
	{
		case nano::block_type::send:
			return true;
		case nano::block_type::state:
		{
			auto const previous = block.previous ();
			if (previous.is_zero ())
			{
				return false; // An open block receives, it never sends.
			}
			auto const previous_balance = ledger.any.block_balance (transaction, previous);
			// Unknown previous: the direction cannot be known, so add no weight.
			return previous_balance.has_value () && block.balance_field ().value () < previous_balance.value ();
		}
		default:
			return false;
	}
}
}

bool nano::block_adds_new_account (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::block const & block)
{
	if (!is_send_unprocessed (ledger, transaction, block))
	{
		return false;
	}
	// A state send carries the destination in its link; a legacy send in destination().
	nano::account destination = block.link_field ().has_value () ? block.link_field ().value ().as_account () : block.destination ();
	// New footprint iff the destination account is not yet opened in the ledger. A dust
	// send to a never-opened account satisfies this on every block (the account never
	// opens), so each such send pays the elevated cost.
	return !ledger.any.account_exists (transaction, destination);
}

nano::storage_weighted_work_result nano::evaluate_storage_weighted_work (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::block const & block, double new_account_multiplier)
{
	nano::storage_weighted_work_result result;
	// Never below 1.0: the storage weight only ever raises the requirement.
	double const multiplier = std::max (1.0, new_account_multiplier);

	result.base_threshold = ledger.work.threshold_base (block.work_version ());
	result.achieved_difficulty = ledger.work.difficulty (block);
	result.creates_new_account = nano::block_adds_new_account (ledger, transaction, block);

	if (result.creates_new_account)
	{
		result.weight_multiplier = multiplier;
		result.required_threshold = nano::difficulty::from_multiplier (multiplier, result.base_threshold);
	}
	else
	{
		result.weight_multiplier = 1.0;
		result.required_threshold = result.base_threshold;
	}
	result.satisfies = result.achieved_difficulty >= result.required_threshold;
	return result;
}
