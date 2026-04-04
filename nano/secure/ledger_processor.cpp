#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_processor.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/block.hpp>
#include <nano/store/ledger/pending.hpp>
#include <nano/store/ledger_store.hpp>

nano::ledger_processor::ledger_processor (nano::secure::write_transaction const & transaction_a, nano::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
{
}

void nano::ledger_processor::send_block (nano::send_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block before? (Harmless)
	if (result == nano::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? nano::block_status::progress : nano::block_status::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result == nano::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? nano::block_status::progress : nano::block_status::block_position;
			if (result == nano::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? nano::block_status::fork : nano::block_status::progress;
				if (result == nano::block_status::progress)
				{
					result = validate_message (account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is this block signed correctly (Malformed)
					if (result == nano::block_status::progress)
					{
						nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == nano::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							release_assert (info->head == block_a.hashables.previous);
							result = info->balance.number () >= block_a.hashables.balance.number () ? nano::block_status::progress : nano::block_status::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result == nano::block_status::progress)
							{
								auto amount (info->balance.number () - block_a.hashables.balance.number ());
								ledger.rep_weights.sub (transaction, info->representative, amount);
								block_a.sideband_set (nano::block_sideband{
								/* account */ account,
								/* balance */ block_a.hashables.balance,
								/* height */ info->block_count + 1,
								/* timestamp */ nano::seconds_since_epoch (),
								/* details */ block_details,
								/* source_epoch */ nano::epoch::epoch_0 });
								ledger.store.block.put (transaction, hash, block_a);
								nano::account_info new_info (hash, info->representative, info->open_block, block_a.hashables.balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
								ledger.update_account (transaction, account, *info, new_info);
								ledger.store.pending.put (transaction, nano::pending_key (block_a.hashables.destination, hash), { account, amount, nano::epoch::epoch_0 });
								ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::send);
							}
						}
					}
				}
			}
		}
	}
}

void nano::ledger_processor::receive_block (nano::receive_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block already?  (Harmless)
	if (result == nano::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? nano::block_status::progress : nano::block_status::gap_previous;
		if (result == nano::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? nano::block_status::progress : nano::block_status::block_position;
			if (result == nano::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? nano::block_status::fork : nano::block_status::progress; // If we have the block but it's not the latest we have a signed fork (Malicious)
				if (result == nano::block_status::progress)
				{
					result = validate_message (account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is the signature valid (Malformed)
					if (result == nano::block_status::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.source) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block already? (Harmless)
						if (result == nano::block_status::progress)
						{
							result = info->head == block_a.hashables.previous ? nano::block_status::progress : nano::block_status::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result == nano::block_status::progress)
							{
								nano::pending_key key (account, block_a.hashables.source);
								auto pending = ledger.store.pending.get (transaction, key);
								result = !pending ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
								if (result == nano::block_status::progress)
								{
									result = pending.value ().epoch == nano::epoch::epoch_0 ? nano::block_status::progress : nano::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result == nano::block_status::progress)
									{
										nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result == nano::block_status::progress)
										{
											auto new_balance (info->balance.number () + pending.value ().amount.number ());
											ledger.store.pending.del (transaction, key);
											block_a.sideband_set (nano::block_sideband{
											/* account */ account,
											/* balance */ new_balance,
											/* height */ info->block_count + 1,
											/* timestamp */ nano::seconds_since_epoch (),
											/* details */ block_details,
											/* source_epoch */ nano::epoch::epoch_0 });
											ledger.store.block.put (transaction, hash, block_a);
											nano::account_info new_info (hash, info->representative, info->open_block, new_balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
											ledger.update_account (transaction, account, *info, new_info);
											ledger.rep_weights.add (transaction, info->representative, pending.value ().amount);
											ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

void nano::ledger_processor::open_block (nano::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block already? (Harmless)
	if (result == nano::block_status::progress)
	{
		result = validate_message (block_a.hashables.account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is the signature valid (Malformed)
		if (result == nano::block_status::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.source) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block? (Harmless)
			if (result == nano::block_status::progress)
			{
				nano::account_info info;
				result = ledger.store.account.get (transaction, block_a.hashables.account, info) ? nano::block_status::progress : nano::block_status::fork; // Has this account already been opened? (Malicious)
				if (result == nano::block_status::progress)
				{
					nano::pending_key key (block_a.hashables.account, block_a.hashables.source);
					auto pending = ledger.store.pending.get (transaction, key);
					result = !pending ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
					if (result == nano::block_status::progress)
					{
						result = block_a.hashables.account == ledger.constants.burn_account ? nano::block_status::opened_burn_account : nano::block_status::progress; // Is it burning 0 account? (Malicious)
						if (result == nano::block_status::progress)
						{
							result = pending.value ().epoch == nano::epoch::epoch_0 ? nano::block_status::progress : nano::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result == nano::block_status::progress)
							{
								nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result == nano::block_status::progress)
								{
									ledger.store.pending.del (transaction, key);
									block_a.sideband_set (nano::block_sideband{
									/* account */ block_a.hashables.account,
									/* balance */ pending.value ().amount,
									/* height */ uint64_t{ 1 },
									/* timestamp */ nano::seconds_since_epoch (),
									/* details */ block_details,
									/* source_epoch */ nano::epoch::epoch_0 });
									ledger.store.block.put (transaction, hash, block_a);
									nano::account_info new_info (hash, block_a.representative_field ().value (), hash, pending.value ().amount.number (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.rep_weights.add (transaction, block_a.representative_field ().value (), pending.value ().amount);
									ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::open);
								}
							}
						}
					}
				}
			}
		}
	}
}

void nano::ledger_processor::change_block (nano::change_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block before? (Harmless)
	if (result == nano::block_status::progress)
	{
		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		result = previous != nullptr ? nano::block_status::progress : nano::block_status::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result == nano::block_status::progress)
		{
			result = block_a.valid_predecessor (*previous) ? nano::block_status::progress : nano::block_status::block_position;
			if (result == nano::block_status::progress)
			{
				auto account = previous->account ();
				auto info = ledger.any.account_get (transaction, account);
				release_assert (info);
				result = info->head != block_a.hashables.previous ? nano::block_status::fork : nano::block_status::progress;
				if (result == nano::block_status::progress)
				{
					release_assert (info->head == block_a.hashables.previous);
					result = validate_message (account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is this block signed correctly (Malformed)
					if (result == nano::block_status::progress)
					{
						nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == nano::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							block_a.sideband_set (nano::block_sideband{
							/* account */ account,
							/* balance */ info->balance,
							/* height */ info->block_count + 1,
							/* timestamp */ nano::seconds_since_epoch (),
							/* details */ block_details,
							/* source_epoch */ nano::epoch::epoch_0 });
							ledger.store.block.put (transaction, hash, block_a);
							auto balance = previous->balance ();
							ledger.rep_weights.move (transaction, info->representative, block_a.hashables.representative, balance);
							nano::account_info new_info (hash, block_a.hashables.representative, info->open_block, info->balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
							ledger.update_account (transaction, account, *info, new_info);
							ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::change);
						}
					}
				}
			}
		}
	}
}

void nano::ledger_processor::state_block (nano::state_block & block_a)
{
	result = nano::block_status::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result == nano::block_status::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void nano::ledger_processor::state_block_impl (nano::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block before? (Unambiguous)
	if (result == nano::block_status::progress)
	{
		result = validate_message (block_a.hashables.account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is this block signed correctly (Unambiguous)
		if (result == nano::block_status::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result = block_a.hashables.account.is_zero () ? nano::block_status::opened_burn_account : nano::block_status::progress; // Is this for the burn account? (Unambiguous)
			if (result == nano::block_status::progress)
			{
				nano::epoch epoch (nano::epoch::epoch_0);
				nano::epoch source_epoch (nano::epoch::epoch_0);
				nano::account_info info;
				nano::amount amount (block_a.hashables.balance);
				auto is_send (false);
				auto is_receive (false);
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					epoch = info.epoch ();
					result = block_a.hashables.previous.is_zero () ? nano::block_status::fork : nano::block_status::progress; // Has this account already been opened? (Ambigious)
					if (result == nano::block_status::progress)
					{
						result = ledger.store.block.exists (transaction, block_a.hashables.previous) ? nano::block_status::progress : nano::block_status::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result == nano::block_status::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							is_receive = !is_send && !block_a.hashables.link.is_zero ();
							amount = is_send ? (info.balance.number () - amount.number ()) : (amount.number () - info.balance.number ());
							result = block_a.hashables.previous == info.head ? nano::block_status::progress : nano::block_status::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result = block_a.previous ().is_zero () ? nano::block_status::progress : nano::block_status::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result == nano::block_status::progress)
					{
						is_receive = true;
						result = !block_a.hashables.link.is_zero () ? nano::block_status::progress : nano::block_status::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result == nano::block_status::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result = ledger.any.block_exists_or_pruned (transaction, block_a.hashables.link.as_block_hash ()) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block already? (Harmless)
							if (result == nano::block_status::progress)
							{
								nano::pending_key key (block_a.hashables.account, block_a.hashables.link.as_block_hash ());
								auto pending = ledger.store.pending.get (transaction, key);
								result = !pending ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
								if (result == nano::block_status::progress)
								{
									result = amount == pending.value ().amount ? nano::block_status::progress : nano::block_status::balance_mismatch;
									source_epoch = pending.value ().epoch;
									epoch = std::max (epoch, source_epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result = amount.is_zero () ? nano::block_status::progress : nano::block_status::balance_mismatch;
						}
					}
				}
				if (result == nano::block_status::progress)
				{
					nano::block_details block_details (epoch, is_send, is_receive, false);
					result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result == nano::block_status::progress)
					{
						ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::state_block);
						block_a.sideband_set (nano::block_sideband{
						/* account */ block_a.hashables.account,
						/* balance */ block_a.hashables.balance,
						/* height */ info.block_count + 1,
						/* timestamp */ nano::seconds_since_epoch (),
						/* details */ block_details,
						/* source_epoch */ source_epoch });
						ledger.store.block.put (transaction, hash, block_a);

						if (!info.head.is_zero ())
						{
							// Move existing representation & add in amount delta
							// ledger.rep_weights.representation_add_dual (transaction, info.representative, 0 - info.balance.number (), block_a.hashables.representative, block_a.hashables.balance.number ());
							ledger.rep_weights.move_add_sub (transaction, info.representative, info.balance, block_a.hashables.representative, block_a.hashables.balance);
						}
						else
						{
							// Add in amount delta only
							ledger.rep_weights.add (transaction, block_a.hashables.representative, block_a.hashables.balance);
						}

						if (is_send)
						{
							nano::pending_key key (block_a.hashables.link.as_account (), hash);
							nano::pending_info info (block_a.hashables.account, amount.number (), epoch);
							ledger.store.pending.put (transaction, key, info);
						}
						else if (!block_a.hashables.link.is_zero ())
						{
							ledger.store.pending.del (transaction, nano::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()));
						}

						nano::account_info new_info (hash, block_a.hashables.representative, info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, nano::seconds_since_epoch (), info.block_count + 1, epoch);
						ledger.update_account (transaction, block_a.hashables.account, info, new_info);
					}
				}
			}
		}
	}
}

void nano::ledger_processor::epoch_block_impl (nano::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing = ledger.any.block_exists_or_pruned (transaction, hash);
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block before? (Unambiguous)
	if (result == nano::block_status::progress)
	{
		result = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is this block signed correctly (Unambiguous)
		if (result == nano::block_status::progress)
		{
			debug_assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result = block_a.hashables.account.is_zero () ? nano::block_status::opened_burn_account : nano::block_status::progress; // Is this for the burn account? (Unambiguous)
			if (result == nano::block_status::progress)
			{
				nano::account_info info;
				auto account_error (ledger.store.account.get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result = block_a.hashables.previous.is_zero () ? nano::block_status::fork : nano::block_status::progress; // Has this account already been opened? (Ambigious)
					if (result == nano::block_status::progress)
					{
						result = block_a.hashables.previous == info.head ? nano::block_status::progress : nano::block_status::fork; // Is the previous block the account's head block? (Ambigious)
						if (result == nano::block_status::progress)
						{
							result = block_a.hashables.representative == info.representative ? nano::block_status::progress : nano::block_status::representative_mismatch;
						}
					}
				}
				else
				{
					result = block_a.hashables.representative.is_zero () ? nano::block_status::progress : nano::block_status::representative_mismatch;
					// Non-exisitng account should have pending entries
					if (result == nano::block_status::progress)
					{
						bool pending_exists = ledger.any.receivable_exists (transaction, block_a.hashables.account);
						result = pending_exists ? nano::block_status::progress : nano::block_status::gap_epoch_open_pending;
					}
				}
				if (result == nano::block_status::progress)
				{
					auto epoch = ledger.constants.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<nano::epoch>> (epoch) > 0 : nano::epochs::is_sequential (info.epoch (), epoch);
					result = is_valid_epoch_upgrade ? nano::block_status::progress : nano::block_status::block_position;
					if (result == nano::block_status::progress)
					{
						result = block_a.hashables.balance == info.balance ? nano::block_status::progress : nano::block_status::balance_mismatch;
						if (result == nano::block_status::progress)
						{
							nano::block_details block_details (epoch, false, false, true);
							result = ledger.work.difficulty (block_a) >= ledger.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result == nano::block_status::progress)
							{
								ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::epoch_block);
								block_a.sideband_set (nano::block_sideband{
								/* account */ block_a.hashables.account,
								/* balance */ block_a.hashables.balance,
								/* height */ info.block_count + 1,
								/* timestamp */ nano::seconds_since_epoch (),
								/* details */ block_details,
								/* source_epoch */ nano::epoch::epoch_0 });
								ledger.store.block.put (transaction, hash, block_a);
								nano::account_info new_info (hash, block_a.hashables.representative, info.open_block.is_zero () ? hash : info.open_block, info.balance, nano::seconds_since_epoch (), info.block_count + 1, epoch);
								ledger.update_account (transaction, block_a.hashables.account, info, new_info);
							}
						}
					}
				}
			}
		}
	}
}

bool nano::ledger_processor::validate_epoch_block (nano::state_block const & block_a)
{
	debug_assert (ledger.is_epoch_link (block_a.hashables.link));
	nano::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result = ledger.store.block.exists (transaction, block_a.hashables.previous) ? nano::block_status::progress : nano::block_status::gap_previous;
		if (result == nano::block_status::progress)
		{
			prev_balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value ();
		}
		else
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link_field ().value ()), block_a.hash (), block_a.signature))
				{
					result = nano::block_status::bad_signature;
				}
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}