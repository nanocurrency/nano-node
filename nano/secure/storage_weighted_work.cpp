#include <nano/lib/blocks.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/storage_weighted_work.hpp>
#include <nano/secure/transaction.hpp>

#include <algorithm>

bool nano::block_adds_new_account (nano::ledger const & ledger, nano::secure::transaction const & transaction, nano::block const & block)
{
	if (!block.is_send ())
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
