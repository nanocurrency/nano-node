#include <nano/lib/block_type.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/files.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/secure/rep_weights.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/final_vote.hpp>
#include <nano/store/online_weight.hpp>
#include <nano/store/peer.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>
#include <nano/store/rep_weight.hpp>
#include <nano/store/version.hpp>

#include <stack>

#include <cryptopp/words.h>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public nano::block_visitor
{
public:
	rollback_visitor (nano::secure::write_transaction const & transaction_a, nano::ledger & ledger_a, std::deque<std::shared_ptr<nano::block>> & list_a, size_t depth_a, size_t max_depth_a) :
		transaction (transaction_a),
		ledger (ledger_a),
		list (list_a),
		depth (depth_a),
		max_depth (max_depth_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (nano::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		nano::pending_key key (block_a.hashables.destination, hash);
		auto pending = ledger.store.pending.get (transaction, key);
		while (!error && !pending.has_value ())
		{
			error = ledger.rollback (transaction, ledger.any.account_head (transaction, block_a.hashables.destination), list, depth + 1, max_depth);
			pending = ledger.store.pending.get (transaction, key);
		}
		if (!error)
		{
			auto info = ledger.any.account_get (transaction, pending.value ().source);
			release_assert (info);
			ledger.store.pending.del (transaction, key);
			ledger.cache.rep_weights.representation_add (transaction, info->representative, pending.value ().amount.number ());
			nano::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.any.block_balance (transaction, block_a.hashables.previous).value (), nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
			ledger.update_account (transaction, pending.value ().source, *info, new_info);
			ledger.store.block.del (transaction, hash);
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::send);
		}
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount = ledger.any.block_amount (transaction, hash).value ().number ();
		auto destination_account = block_a.account ();
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		auto source_account = ledger.any.block_account (transaction, block_a.hashables.source);
		auto info = ledger.any.account_get (transaction, destination_account);
		release_assert (info);
		ledger.cache.rep_weights.representation_add (transaction, info->representative, 0 - amount);
		nano::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.any.block_balance (transaction, block_a.hashables.previous).value (), nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
		ledger.update_account (transaction, destination_account, *info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, nano::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, nano::epoch::epoch_0 });
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::receive);
	}
	void open_block (nano::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount = ledger.any.block_amount (transaction, hash).value ().number ();
		auto destination_account = block_a.account ();
		auto source_account = ledger.any.block_account (transaction, block_a.hashables.source);
		ledger.cache.rep_weights.representation_add (transaction, block_a.representative_field ().value (), 0 - amount);
		nano::account_info new_info;
		ledger.update_account (transaction, destination_account, new_info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, nano::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, nano::epoch::epoch_0 });
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::open);
	}
	void change_block (nano::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account = block_a.account ();
		auto info = ledger.any.account_get (transaction, account);
		release_assert (info);
		auto balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value ();
		auto block = ledger.store.block.get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative_field ().value ();
		ledger.cache.rep_weights.representation_add_dual (transaction, block_a.hashables.representative, 0 - balance.number (), representative, balance.number ());
		ledger.store.block.del (transaction, hash);
		nano::account_info new_info (block_a.hashables.previous, representative, info->open_block, info->balance, nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
		ledger.update_account (transaction, account, *info, new_info);
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::change);
	}
	void state_block (nano::state_block const & block_a) override
	{
		auto const hash = block_a.hash ();
		auto const previous_balance = ledger.any.block_balance (transaction, block_a.hashables.previous).value_or (0).number ();
		bool const is_send = block_a.hashables.balance < previous_balance;

		auto info = ledger.any.account_get (transaction, block_a.hashables.account);
		release_assert (info);

		if (is_send)
		{
			nano::pending_key key (block_a.hashables.link.as_account (), hash);
			while (!error && !ledger.any.pending_get (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.any.account_head (transaction, block_a.hashables.link.as_account ()), list, depth + 1, max_depth);
			}
			if (error)
			{
				return;
			}
			ledger.store.pending.del (transaction, key);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
			auto source_account = ledger.any.block_account (transaction, block_a.hashables.link.as_block_hash ());
			nano::pending_info pending_info (source_account.value_or (0), block_a.hashables.balance.number () - previous_balance, block_a.sideband ().source_epoch);
			ledger.store.pending.put (transaction, nano::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()), pending_info);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::receive);
		}

		release_assert (!error);

		nano::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}

		nano::account representative{};
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation & add in amount delta
			auto rep_block (ledger.store.block.get (transaction, rep_block_hash));
			release_assert (rep_block != nullptr);
			representative = rep_block->representative_field ().value ();
			ledger.cache.rep_weights.representation_add_dual (transaction, representative, previous_balance, block_a.hashables.representative, 0 - block_a.hashables.balance.number ());
		}
		else
		{
			// Add in amount delta only
			ledger.cache.rep_weights.representation_add (transaction, block_a.hashables.representative, 0 - block_a.hashables.balance.number ());
		}

		auto previous_version (ledger.version (transaction, block_a.hashables.previous));
		nano::account_info new_info (block_a.hashables.previous, representative, info->open_block, previous_balance, nano::seconds_since_epoch (), info->block_count - 1, previous_version);
		ledger.update_account (transaction, block_a.hashables.account, *info, new_info);

		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		}
		else
		{
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::open);
		}
		ledger.store.block.del (transaction, hash);
	}
	nano::secure::write_transaction const & transaction;
	nano::ledger & ledger;
	std::deque<std::shared_ptr<nano::block>> & list;
	size_t const depth;
	size_t const max_depth;
	bool error{ false };
};

class ledger_processor : public nano::mutable_block_visitor
{
public:
	ledger_processor (nano::ledger &, nano::secure::write_transaction const &);
	virtual ~ledger_processor () = default;
	void send_block (nano::send_block &) override;
	void receive_block (nano::receive_block &) override;
	void open_block (nano::open_block &) override;
	void change_block (nano::change_block &) override;
	void state_block (nano::state_block &) override;
	void state_block_impl (nano::state_block &);
	void epoch_block_impl (nano::state_block &);
	nano::ledger & ledger;
	nano::secure::write_transaction const & transaction;
	nano::block_status result;

private:
	bool validate_epoch_block (nano::state_block const & block_a);
};

// Returns true if this block which has an epoch link is correctly formed.
bool ledger_processor::validate_epoch_block (nano::state_block const & block_a)
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

void ledger_processor::state_block (nano::state_block & block_a)
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

void ledger_processor::state_block_impl (nano::state_block & block_a)
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
					result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result == nano::block_status::progress)
					{
						ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::state_block);
						block_a.sideband_set (nano::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, nano::seconds_since_epoch (), block_details, source_epoch));
						ledger.store.block.put (transaction, hash, block_a);

						if (!info.head.is_zero ())
						{
							// Move existing representation & add in amount delta
							ledger.cache.rep_weights.representation_add_dual (transaction, info.representative, 0 - info.balance.number (), block_a.hashables.representative, block_a.hashables.balance.number ());
						}
						else
						{
							// Add in amount delta only
							ledger.cache.rep_weights.representation_add (transaction, block_a.hashables.representative, block_a.hashables.balance.number ());
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

void ledger_processor::epoch_block_impl (nano::state_block & block_a)
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
							result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result == nano::block_status::progress)
							{
								ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::epoch_block);
								block_a.sideband_set (nano::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
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

void ledger_processor::change_block (nano::change_block & block_a)
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
						result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == nano::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							block_a.sideband_set (nano::block_sideband (account, 0, info->balance, info->block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
							ledger.store.block.put (transaction, hash, block_a);
							auto balance = previous->balance ();
							ledger.cache.rep_weights.representation_add_dual (transaction, block_a.hashables.representative, balance.number (), info->representative, 0 - balance.number ());
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

void ledger_processor::send_block (nano::send_block & block_a)
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
						result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result == nano::block_status::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							release_assert (info->head == block_a.hashables.previous);
							result = info->balance.number () >= block_a.hashables.balance.number () ? nano::block_status::progress : nano::block_status::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result == nano::block_status::progress)
							{
								auto amount (info->balance.number () - block_a.hashables.balance.number ());
								ledger.cache.rep_weights.representation_add (transaction, info->representative, 0 - amount);
								block_a.sideband_set (nano::block_sideband (account, 0, block_a.hashables.balance /* unused */, info->block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
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

void ledger_processor::receive_block (nano::receive_block & block_a)
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
										result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result == nano::block_status::progress)
										{
											auto new_balance (info->balance.number () + pending.value ().amount.number ());
											ledger.store.pending.del (transaction, key);
											block_a.sideband_set (nano::block_sideband (account, 0, new_balance, info->block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
											ledger.store.block.put (transaction, hash, block_a);
											nano::account_info new_info (hash, info->representative, info->open_block, new_balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
											ledger.update_account (transaction, account, *info, new_info);
											ledger.cache.rep_weights.representation_add (transaction, info->representative, pending.value ().amount.number ());
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

void ledger_processor::open_block (nano::open_block & block_a)
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
								result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result == nano::block_status::progress)
								{
									ledger.store.pending.del (transaction, key);
									block_a.sideband_set (nano::block_sideband (block_a.hashables.account, 0, pending.value ().amount, 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
									ledger.store.block.put (transaction, hash, block_a);
									nano::account_info new_info (hash, block_a.representative_field ().value (), hash, pending.value ().amount.number (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.cache.rep_weights.representation_add (transaction, block_a.representative_field ().value (), pending.value ().amount.number ());
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

ledger_processor::ledger_processor (nano::ledger & ledger_a, nano::secure::write_transaction const & transaction_a) :
	ledger (ledger_a),
	transaction (transaction_a)
{
}

/**
 * Determine the representative for this block
 */
class representative_visitor final : public nano::block_visitor
{
public:
	representative_visitor (nano::secure::transaction const & transaction_a, nano::ledger & ledger);
	~representative_visitor () = default;
	void compute (nano::block_hash const & hash_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::secure::transaction const & transaction;
	nano::ledger & ledger;
	nano::block_hash current;
	nano::block_hash result;
};

representative_visitor::representative_visitor (nano::secure::transaction const & transaction_a, nano::ledger & ledger) :
	transaction{ transaction_a },
	ledger{ ledger },
	result{ 0 }
{
}

void representative_visitor::compute (nano::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block_l = ledger.any.block_get (transaction, current);
		release_assert (block_l != nullptr);
		block_l->visit (*this);
	}
}

void representative_visitor::send_block (nano::send_block const & block_a)
{
	current = block_a.previous ();
}

void representative_visitor::receive_block (nano::receive_block const & block_a)
{
	current = block_a.previous ();
}

void representative_visitor::open_block (nano::open_block const & block_a)
{
	result = block_a.hash ();
}

void representative_visitor::change_block (nano::change_block const & block_a)
{
	result = block_a.hash ();
}

void representative_visitor::state_block (nano::state_block const & block_a)
{
	result = block_a.hash ();
}
}

/*
 * ledger
 */

nano::ledger::ledger (nano::store::component & store_a, nano::ledger_constants & constants_a, nano::stats & stats_a, nano::logger & logger_a, nano::generate_cache_flags const & generate_cache_flags_a, nano::uint128_t min_rep_weight_a) :
	constants{ constants_a },
	store{ store_a },
	cache{ store_a.rep_weight, min_rep_weight_a },
	stats{ stats_a },
	logger{ logger_a },
	any_impl{ std::make_unique<ledger_set_any> (*this) },
	confirmed_impl{ std::make_unique<ledger_set_confirmed> (*this) },
	any{ *any_impl },
	confirmed{ *confirmed_impl }
{
	if (!store.init_error ())
	{
		initialize (generate_cache_flags_a);
	}
}

nano::ledger::~ledger ()
{
}

auto nano::ledger::tx_begin_write (nano::store::writer guard_type) const -> secure::write_transaction
{
	auto guard = store.write_queue.wait (guard_type);
	auto txn = store.tx_begin_write ();
	return secure::write_transaction{ std::move (txn), std::move (guard) };
}

auto nano::ledger::tx_begin_read () const -> secure::read_transaction
{
	return secure::read_transaction{ store.tx_begin_read () };
}

void nano::ledger::initialize (nano::generate_cache_flags const & generate_cache_flags_a)
{
	logger.info (nano::log::type::ledger, "Loading ledger, this may take a while...");

	if (generate_cache_flags_a.reps || generate_cache_flags_a.account_count || generate_cache_flags_a.block_count)
	{
		store.account.for_each_par (
		[this] (store::read_transaction const & /*unused*/, auto i, auto n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			for (; i != n; ++i)
			{
				nano::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
		});

		store.rep_weight.for_each_par (
		[this] (store::read_transaction const & /*unused*/, auto i, auto n) {
			nano::rep_weights rep_weights_l{ this->store.rep_weight };
			for (; i != n; ++i)
			{
				rep_weights_l.representation_put (i->first, i->second.number ());
			}
			this->cache.rep_weights.copy_from (rep_weights_l);
		});
	}

	if (generate_cache_flags_a.cemented_count)
	{
		store.confirmation_height.for_each_par (
		[this] (store::read_transaction const & /*unused*/, auto i, auto n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});
	}

	{
		auto transaction (store.tx_begin_read ());
		cache.pruned_count = store.pruned.count (transaction);
	}

	logger.info (nano::log::type::ledger, "Block count:    {:>11}", cache.block_count.load ());
	logger.info (nano::log::type::ledger, "Cemented count: {:>11}", cache.cemented_count.load ());
	logger.info (nano::log::type::ledger, "Account count:  {:>11}", cache.account_count.load ());
	logger.info (nano::log::type::ledger, "Pruned count:   {:>11}", cache.pruned_count.load ());
	logger.info (nano::log::type::ledger, "Representative count: {:>5}", cache.rep_weights.size ());
}

bool nano::ledger::unconfirmed_exists (secure::transaction const & transaction, nano::block_hash const & hash)
{
	return any.block_exists (transaction, hash) && !confirmed.block_exists (transaction, hash);
}

nano::uint128_t nano::ledger::account_receivable (secure::transaction const & transaction_a, nano::account const & account_a, bool only_confirmed_a)
{
	nano::uint128_t result (0);
	nano::account end (account_a.number () + 1);
	for (auto i (store.pending.begin (transaction_a, nano::pending_key (account_a, 0))), n (store.pending.begin (transaction_a, nano::pending_key (end, 0))); i != n; ++i)
	{
		nano::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (confirmed.block_exists_or_pruned (transaction_a, i->first.hash))
			{
				result += info.amount.number ();
			}
		}
		else
		{
			result += info.amount.number ();
		}
	}
	return result;
}

// Both stack and result set are bounded to limit maximum memory usage
// Callers must ensure that the target block was confirmed, and if not, call this function multiple times
std::deque<std::shared_ptr<nano::block>> nano::ledger::confirm (secure::write_transaction & transaction, nano::block_hash const & target_hash, size_t max_blocks)
{
	std::deque<std::shared_ptr<nano::block>> result;

	std::deque<nano::block_hash> stack;
	stack.push_back (target_hash);
	while (!stack.empty ())
	{
		auto hash = stack.back ();
		auto block = any.block_get (transaction, hash);
		release_assert (block);

		auto dependents = dependent_blocks (transaction, *block);
		for (auto const & dependent : dependents)
		{
			if (!dependent.is_zero () && !confirmed.block_exists_or_pruned (transaction, dependent))
			{
				stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::dependent_unconfirmed);

				stack.push_back (dependent);

				// Limit the stack size to avoid excessive memory usage
				// This will forget the bottom of the dependency tree
				if (stack.size () > max_blocks)
				{
					stack.pop_front ();
				}
			}
		}

		if (stack.back () == hash)
		{
			stack.pop_back ();
			if (!confirmed.block_exists_or_pruned (transaction, hash))
			{
				// We must only confirm blocks that have their dependencies confirmed
				debug_assert (dependents_confirmed (transaction, *block));
				confirm_one (transaction, *block);
				result.push_back (block);
			}
		}
		else
		{
			// Unconfirmed dependencies were added
		}

		// Refresh the transaction to avoid long-running transactions
		// Ensure that the block wasn't rolled back during the refresh
		bool refreshed = transaction.refresh_if_needed ();
		if (refreshed)
		{
			if (!any.block_exists (transaction, target_hash))
			{
				break; // Block was rolled back during cementing
			}
		}

		// Early return might leave parts of the dependency tree unconfirmed
		if (result.size () >= max_blocks)
		{
			break;
		}
	}

	return result;
}

void nano::ledger::confirm_one (secure::write_transaction & transaction, nano::block const & block)
{
	debug_assert ((!store.confirmation_height.get (transaction, block.account ()) && block.sideband ().height == 1) || store.confirmation_height.get (transaction, block.account ()).value ().height + 1 == block.sideband ().height);
	confirmation_height_info info{ block.sideband ().height, block.hash () };
	store.confirmation_height.put (transaction, block.account (), info);
	++cache.cemented_count;

	stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed);
}

nano::block_status nano::ledger::process (secure::write_transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
{
	debug_assert (!constants.work.validate_entry (*block_a) || constants.genesis == nano::dev::genesis);
	ledger_processor processor (*this, transaction_a);
	block_a->visit (processor);
	if (processor.result == nano::block_status::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

nano::block_hash nano::ledger::representative (secure::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || any.block_exists (transaction_a, result));
	return result;
}

nano::block_hash nano::ledger::representative_calculated (secure::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.result;
}

std::string nano::ledger::block_text (char const * hash_a)
{
	return block_text (nano::block_hash (hash_a));
}

std::string nano::ledger::block_text (nano::block_hash const & hash_a)
{
	std::string result;
	auto transaction = tx_begin_read ();
	auto block_l = any.block_get (transaction, hash_a);
	if (block_l != nullptr)
	{
		block_l->serialize_json (result);
	}
	return result;
}

std::deque<std::shared_ptr<nano::block>> nano::ledger::random_blocks (secure::transaction const & transaction, size_t count) const
{
	std::deque<std::shared_ptr<nano::block>> result;

	auto const starting_hash = nano::random_pool::generate<nano::block_hash> ();

	// It is more efficient to choose a random starting point and pick a few sequential blocks from there
	auto it = store.block.begin (transaction, starting_hash);
	auto const end = store.block.end (transaction);
	while (result.size () < count)
	{
		if (it != end)
		{
			result.push_back (it->second.block);
		}
		++it; // Store iterators wrap around when reaching the end
	}

	return result;
}

bool nano::ledger::bootstrap_height_reached () const
{
	return cache.block_count >= bootstrap_weight_max_blocks;
}

std::unordered_map<nano::account, nano::uint128_t> nano::ledger::rep_weights_snapshot () const
{
	if (!bootstrap_height_reached ())
	{
		return bootstrap_weights;
	}
	else
	{
		return cache.rep_weights.get_rep_amounts ();
	}
}

nano::uint128_t nano::ledger::weight (nano::account const & account_a) const
{
	if (!bootstrap_height_reached ())
	{
		auto weight = bootstrap_weights.find (account_a);
		if (weight != bootstrap_weights.end ())
		{
			return weight->second;
		}
		return 0;
	}
	else
	{
		return cache.rep_weights.representation_get (account_a);
	}
}

nano::uint128_t nano::ledger::weight_exact (secure::transaction const & txn_a, nano::account const & representative_a) const
{
	return store.rep_weight.get (txn_a, representative_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
// TODO: Refactor rollback operation to use non-recursive algorithm
bool nano::ledger::rollback (secure::write_transaction const & transaction_a, nano::block_hash const & block_a, std::deque<std::shared_ptr<nano::block>> & list_a, size_t depth, size_t const max_depth)
{
	if (depth > max_depth)
	{
		logger.critical (nano::log::type::ledger, "Rollback depth exceeded: {} (max depth: {})", depth, max_depth);
		return true; // Error
	}

	debug_assert (any.block_exists (transaction_a, block_a));
	auto account_l = any.block_account (transaction_a, block_a).value ();
	auto block_account_height (any.block_height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a, depth, max_depth);
	auto error (false);
	while (!error && any.block_exists (transaction_a, block_a))
	{
		nano::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto info = any.account_get (transaction_a, account_l);
			release_assert (info);
			auto block_l = any.block_get (transaction_a, info->head);
			release_assert (block_l != nullptr);
			block_l->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				list_a.push_back (block_l);
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool nano::ledger::rollback (secure::write_transaction const & transaction_a, nano::block_hash const & block_a)
{
	std::deque<std::shared_ptr<nano::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return latest root for account, account number if there are no blocks for this account.
nano::root nano::ledger::latest_root (secure::transaction const & transaction_a, nano::account const & account_a)
{
	auto info = any.account_get (transaction_a, account_a);
	if (!info)
	{
		return account_a;
	}
	else
	{
		return info->head;
	}
}

void nano::ledger::dump_account_chain (nano::account const & account_a, std::ostream & stream)
{
	auto transaction = tx_begin_read ();
	auto hash (any.account_head (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block_l = any.block_get (transaction, hash);
		debug_assert (block_l != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block_l->previous ();
	}
}

bool nano::ledger::dependents_confirmed (secure::transaction const & transaction_a, nano::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (nano::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = confirmed.block_exists_or_pruned (transaction_a, hash_a);
		}
		return result;
	});
}

bool nano::ledger::is_epoch_link (nano::link const & link_a) const
{
	return constants.epochs.is_epoch_link (link_a);
}

class dependent_block_visitor : public nano::block_visitor
{
public:
	dependent_block_visitor (nano::ledger const & ledger_a, nano::secure::transaction const & transaction_a) :
		ledger (ledger_a),
		transaction (transaction_a),
		result ({ 0, 0 })
	{
	}
	void send_block (nano::send_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		result[0] = block_a.previous ();
		result[1] = block_a.source_field ().value ();
	}
	void open_block (nano::open_block const & block_a) override
	{
		if (block_a.source_field ().value () != ledger.constants.genesis->account ().as_union ())
		{
			result[0] = block_a.source_field ().value ();
		}
	}
	void change_block (nano::change_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void state_block (nano::state_block const & block_a) override
	{
		result[0] = block_a.hashables.previous;
		result[1] = block_a.hashables.link.as_block_hash ();
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block_a.hashables.link) || is_send (block_a))
		{
			result[1].clear ();
		}
	}
	// This function is used in place of block->is_send () as it is tolerant to the block not having the sideband information loaded
	// This is needed for instance in vote generation on forks which have not yet had sideband information attached
	bool is_send (nano::state_block const & block) const
	{
		if (block.previous ().is_zero ())
		{
			return false;
		}
		if (block.has_sideband ())
		{
			return block.sideband ().details.is_send;
		}
		return block.balance_field ().value () < ledger.any.block_balance (transaction, block.previous ());
	}
	nano::ledger const & ledger;
	nano::secure::transaction const & transaction;
	std::array<nano::block_hash, 2> result;
};

std::array<nano::block_hash, 2> nano::ledger::dependent_blocks (secure::transaction const & transaction_a, nano::block const & block_a) const
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

/** Given the block hash of a send block, find the associated receive block that receives that send.
 *  The send block hash is not checked in any way, it is assumed to be correct.
 * @return Return the receive block on success and null on failure
 */
std::shared_ptr<nano::block> nano::ledger::find_receive_block_by_send_hash (secure::transaction const & transaction, nano::account const & destination, nano::block_hash const & send_block_hash)
{
	std::shared_ptr<nano::block> result;
	debug_assert (send_block_hash != 0);

	// get the cemented frontier
	nano::confirmation_height_info info;
	if (store.confirmation_height.get (transaction, destination, info))
	{
		return nullptr;
	}
	auto possible_receive_block = any.block_get (transaction, info.frontier);

	// walk down the chain until the source field of a receive block matches the send block hash
	while (possible_receive_block != nullptr)
	{
		if (possible_receive_block->is_receive () && send_block_hash == possible_receive_block->source ())
		{
			// we have a match
			result = possible_receive_block;
			break;
		}

		possible_receive_block = any.block_get (transaction, possible_receive_block->previous ());
	}

	return result;
}

std::optional<nano::account> nano::ledger::linked_account (secure::transaction const & transaction, nano::block const & block)
{
	if (block.is_send ())
	{
		return block.destination ();
	}
	else if (block.is_receive ())
	{
		return any.block_account (transaction, block.source ());
	}

	return std::nullopt;
}

nano::account const & nano::ledger::epoch_signer (nano::link const & link_a) const
{
	return constants.epochs.signer (constants.epochs.epoch (link_a));
}

nano::link const & nano::ledger::epoch_link (nano::epoch epoch_a) const
{
	return constants.epochs.link (epoch_a);
}

void nano::ledger::update_account (secure::write_transaction const & transaction_a, nano::account const & account_a, nano::account_info const & old_a, nano::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account.put won't erase existing entries if they're in different tables
			store.account.del (transaction_a, account_a);
		}
		store.account.put (transaction_a, account_a, new_a);
	}
	else
	{
		debug_assert (!store.confirmation_height.exists (transaction_a, account_a));
		store.account.del (transaction_a, account_a);
		release_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<nano::block> nano::ledger::forked_block (secure::transaction const & transaction_a, nano::block const & block_a)
{
	debug_assert (!any.block_exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (any.block_exists (transaction_a, root.as_block_hash ()) || store.account.exists (transaction_a, root.as_account ()));
	std::shared_ptr<nano::block> result;
	auto successor_l = any.block_successor (transaction_a, root.as_block_hash ());
	if (successor_l)
	{
		result = any.block_get (transaction_a, successor_l.value ());
	}
	if (result == nullptr)
	{
		auto info = any.account_get (transaction_a, root.as_account ());
		release_assert (info);
		result = any.block_get (transaction_a, info->open_block);
		release_assert (result != nullptr);
	}
	return result;
}

uint64_t nano::ledger::pruning_action (secure::write_transaction & transaction_a, nano::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	nano::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != constants.genesis->hash ())
	{
		auto block_l = any.block_get (transaction_a, hash);
		if (block_l != nullptr)
		{
			release_assert (confirmed.block_exists (transaction_a, hash));
			store.block.del (transaction_a, hash);
			store.pruned.put (transaction_a, hash);
			hash = block_l->previous ();
			++pruned_count;
			++cache.pruned_count;
			if (pruned_count % batch_size_a == 0)
			{
				transaction_a.commit ();
				transaction_a.renew ();
			}
		}
		else if (store.pruned.exists (transaction_a, hash))
		{
			hash = 0;
		}
		else
		{
			hash = 0;
			release_assert (false, "error finding block for pruning");
		}
	}
	return pruned_count;
}

auto nano::ledger::block_priority (nano::secure::transaction const & transaction, nano::block const & block) const -> block_priority_result
{
	auto const balance = block.balance ();
	auto const previous_block = !block.previous ().is_zero () ? any.block_get (transaction, block.previous ()) : nullptr;
	auto const previous_balance = previous_block ? previous_block->balance () : 0;

	// Handle full send case nicely where the balance would otherwise be 0
	auto const priority_balance = std::max (balance, block.is_send () ? previous_balance : 0);

	// Use previous block timestamp as priority timestamp for least recently used prioritization within the same bucket
	// Account info timestamp is not used here because it will get out of sync when rollbacks happen
	auto const priority_timestamp = previous_block ? previous_block->sideband ().timestamp : block.sideband ().timestamp;
	return { priority_balance, priority_timestamp };
}

// A precondition is that the store is an LMDB store
bool nano::ledger::migrate_lmdb_to_rocksdb (std::filesystem::path const & data_path_a) const
{
	logger.info (nano::log::type::ledger, "Migrating LMDB database to RocksDB. This will take a while...");

	std::filesystem::space_info si = std::filesystem::space (data_path_a);
	auto file_size = std::filesystem::file_size (data_path_a / "data.ldb");
	const auto estimated_required_space = file_size * 0.65; // RocksDb database size is approximately 65% of the lmdb size

	if (si.available < estimated_required_space)
	{
		logger.warn (nano::log::type::ledger, "You may not have enough available disk space. Estimated free space requirement is {} GB", estimated_required_space / 1024 / 1024 / 1024);
	}

	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path_a, error_chmod);
	auto rockdb_data_path = data_path_a / "rocksdb";

	if (std::filesystem::exists (rockdb_data_path))
	{
		logger.error (nano::log::type::ledger, "Existing RocksDB folder found in '{}'. Please remove it and try again.", rockdb_data_path.string ());
		return true;
	}

	auto error (false);

	// Open rocksdb database
	nano::node_config node_config;
	node_config.database_backend = database_backend::rocksdb;
	auto rocksdb_store = nano::make_store (logger, data_path_a, nano::dev::constants, false, true, node_config);

	if (!rocksdb_store->init_error ())
	{
		auto table_size = store.count (store.tx_begin_read (), tables::blocks);
		logger.info (nano::log::type::ledger, "Step 1 of 7: Converting {} entries from blocks table", table_size);
		std::atomic<std::size_t> count = 0;
		store.block.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				std::vector<uint8_t> vector;
				{
					nano::vectorstream stream (vector);
					nano::serialize_block (stream, *i->second.block);
					i->second.sideband.serialize (stream, i->second.block->type ());
				}
				rocksdb_store->block.raw_put (rocksdb_transaction, vector, i->first);

				if (auto count_l = ++count; count_l % 5000000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} blocks converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::pending);
		logger.info (nano::log::type::ledger, "Step 2 of 7: Converting {} entries from pending table", table_size);
		count = 0;
		store.pending.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->pending.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::confirmation_height);
		logger.info (nano::log::type::ledger, "Step 3 of 7: Converting {} entries from confirmation_height table", table_size);
		count = 0;
		store.confirmation_height.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->confirmation_height.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::accounts);
		logger.info (nano::log::type::ledger, "Step 4 of 7: Converting {} entries from accounts table", table_size);
		count = 0;
		store.account.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->account.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::rep_weights);
		logger.info (nano::log::type::ledger, "Step 5 of 7: Converting {} entries from rep_weights table", table_size);
		count = 0;
		store.rep_weight.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->rep_weight.put (rocksdb_transaction, i->first, i->second.number ());
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::pruned);
		logger.info (nano::log::type::ledger, "Step 6 of 7: Converting {} entries from pruned table", table_size);
		count = 0;
		store.pruned.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->pruned.put (rocksdb_transaction, i->first);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		table_size = store.count (store.tx_begin_read (), tables::final_votes);
		logger.info (nano::log::type::ledger, "Step 7 of 7: Converting {} entries from final_votes table", table_size);
		count = 0;
		store.final_vote.for_each_par (
		[&] (store::read_transaction const & /*unused*/, auto i, auto n) {
			auto rocksdb_transaction = rocksdb_store->tx_begin_write ();
			for (; i != n; ++i)
			{
				rocksdb_transaction.refresh_if_needed ();
				rocksdb_store->final_vote.put (rocksdb_transaction, i->first, i->second);
				if (auto count_l = ++count; count_l % 500000 == 0)
				{
					logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count_l, count_l * 100 / table_size);
				}
			}
		});
		logger.info (nano::log::type::ledger, "{} entries converted ({}%)", count.load (), table_size > 0 ? count.load () * 100 / table_size : 100);

		logger.info (nano::log::type::ledger, "Finalizing migration...");

		auto lmdb_transaction (tx_begin_read ());
		auto version = store.version.get (lmdb_transaction);
		auto rocksdb_transaction (rocksdb_store->tx_begin_write ());
		rocksdb_store->version.put (rocksdb_transaction, version);

		for (auto i (store.online_weight.begin (lmdb_transaction)), n (store.online_weight.end (lmdb_transaction)); i != n; ++i)
		{
			rocksdb_store->online_weight.put (rocksdb_transaction, i->first, i->second);
		}

		for (auto i (store.peer.begin (lmdb_transaction)), n (store.peer.end (lmdb_transaction)); i != n; ++i)
		{
			rocksdb_store->peer.put (rocksdb_transaction, i->first, i->second);
		}

		// Compare counts
		error |= store.peer.count (lmdb_transaction) != rocksdb_store->peer.count (rocksdb_transaction);
		error |= store.pruned.count (lmdb_transaction) != rocksdb_store->pruned.count (rocksdb_transaction);
		error |= store.final_vote.count (lmdb_transaction) != rocksdb_store->final_vote.count (rocksdb_transaction);
		error |= store.online_weight.count (lmdb_transaction) != rocksdb_store->online_weight.count (rocksdb_transaction);
		error |= store.rep_weight.count (lmdb_transaction) != rocksdb_store->rep_weight.count (rocksdb_transaction);
		error |= store.version.get (lmdb_transaction) != rocksdb_store->version.get (rocksdb_transaction);

		// For large tables a random key is used instead and makes sure it exists
		auto blocks = random_blocks (lmdb_transaction, 42);
		release_assert (!blocks.empty ());
		for (auto const & block : blocks)
		{
			auto const account = block->account ();

			error |= rocksdb_store->block.get (rocksdb_transaction, block->hash ()) == nullptr;

			nano::account_info account_info;
			error |= rocksdb_store->account.get (rocksdb_transaction, account, account_info);

			// If confirmation height exists in the lmdb ledger for this account it should exist in the rocksdb ledger
			nano::confirmation_height_info confirmation_height_info{};
			if (!store.confirmation_height.get (lmdb_transaction, account, confirmation_height_info))
			{
				error |= rocksdb_store->confirmation_height.get (rocksdb_transaction, account, confirmation_height_info);
			}
		}

		logger.info (nano::log::type::ledger, "Migration completed. Make sure to set `database_backend` under [node] to 'rocksdb' in config-node.toml");
		logger.info (nano::log::type::ledger, "After confirming correct node operation, the data.ldb file can be deleted if no longer required");
	}
	else
	{
		error = true;
	}
	return error;
}

nano::epoch nano::ledger::version (nano::block const & block)
{
	if (block.type () == nano::block_type::state)
	{
		return block.sideband ().details.epoch;
	}

	return nano::epoch::epoch_0;
}

nano::epoch nano::ledger::version (secure::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = any.block_get (transaction, hash);
	if (block_l == nullptr)
	{
		return nano::epoch::epoch_0;
	}
	return version (*block_l);
}

uint64_t nano::ledger::cemented_count () const
{
	return cache.cemented_count;
}

uint64_t nano::ledger::block_count () const
{
	return cache.block_count;
}

uint64_t nano::ledger::account_count () const
{
	return cache.account_count;
}

uint64_t nano::ledger::pruned_count () const
{
	return cache.pruned_count;
}

uint64_t nano::ledger::backlog_count () const
{
	auto blocks = cache.block_count.load ();
	auto cemented = cache.cemented_count.load ();
	return (blocks > cemented) ? blocks - cemented : 0;
}

nano::container_info nano::ledger::container_info () const
{
	nano::container_info info;
	info.put ("bootstrap_weights", bootstrap_weights);
	info.add ("rep_weights", cache.rep_weights.container_info ());
	return info;
}
