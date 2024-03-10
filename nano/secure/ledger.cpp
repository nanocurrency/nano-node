#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/rep_weights.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/node/make_store.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>
#include <nano/store/final.hpp>
#include <nano/store/frontier.hpp>
#include <nano/store/online_weight.hpp>
#include <nano/store/peer.hpp>
#include <nano/store/pending.hpp>
#include <nano/store/pruned.hpp>
#include <nano/store/version.hpp>

#include <cryptopp/words.h>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public nano::block_visitor
{
public:
	rollback_visitor (nano::store::write_transaction const & transaction_a, nano::ledger & ledger_a, std::vector<std::shared_ptr<nano::block>> & list_a) :
		transaction (transaction_a),
		ledger (ledger_a),
		list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (nano::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		nano::pending_info pending;
		nano::pending_key key (block_a.hashables.destination, hash);
		while (!error && ledger.store.pending.get (transaction, key, pending))
		{
			error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		if (!error)
		{
			auto info = ledger.account_info (transaction, pending.source);
			debug_assert (info);
			ledger.store.pending.del (transaction, key);
			ledger.cache.rep_weights.representation_add (info->representative, pending.amount.number ());
			nano::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.balance (transaction, block_a.hashables.previous).value (), nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
			ledger.update_account (transaction, pending.source, *info, new_info);
			ledger.store.block.del (transaction, hash);
			ledger.store.frontier.del (transaction, hash);
			ledger.store.frontier.put (transaction, block_a.hashables.previous, pending.source);
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::send);
		}
	}
	void receive_block (nano::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount = ledger.amount (transaction, hash).value ();
		auto destination_account = block_a.account ();
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		auto source_account = ledger.account (transaction, block_a.hashables.source);
		auto info = ledger.account_info (transaction, destination_account);
		debug_assert (info);
		ledger.cache.rep_weights.representation_add (info->representative, 0 - amount);
		nano::account_info new_info (block_a.hashables.previous, info->representative, info->open_block, ledger.balance (transaction, block_a.hashables.previous).value (), nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
		ledger.update_account (transaction, destination_account, *info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, nano::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, nano::epoch::epoch_0 });
		ledger.store.frontier.del (transaction, hash);
		ledger.store.frontier.put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::receive);
	}
	void open_block (nano::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount = ledger.amount (transaction, hash).value ();
		auto destination_account = block_a.account ();
		auto source_account = ledger.account (transaction, block_a.hashables.source);
		ledger.cache.rep_weights.representation_add (block_a.representative_field ().value (), 0 - amount);
		nano::account_info new_info;
		ledger.update_account (transaction, destination_account, new_info, new_info);
		ledger.store.block.del (transaction, hash);
		ledger.store.pending.put (transaction, nano::pending_key (destination_account, block_a.hashables.source), { source_account.value_or (0), amount, nano::epoch::epoch_0 });
		ledger.store.frontier.del (transaction, hash);
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::open);
	}
	void change_block (nano::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account = block_a.account ();
		auto info = ledger.account_info (transaction, account);
		debug_assert (info);
		auto balance = ledger.balance (transaction, block_a.hashables.previous).value ();
		auto block = ledger.store.block.get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative_field ().value ();
		ledger.cache.rep_weights.representation_add_dual (block_a.hashables.representative, 0 - balance, representative, balance);
		ledger.store.block.del (transaction, hash);
		nano::account_info new_info (block_a.hashables.previous, representative, info->open_block, info->balance, nano::seconds_since_epoch (), info->block_count - 1, nano::epoch::epoch_0);
		ledger.update_account (transaction, account, *info, new_info);
		ledger.store.frontier.del (transaction, hash);
		ledger.store.frontier.put (transaction, block_a.hashables.previous, account);
		ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::change);
	}
	void state_block (nano::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		nano::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}
		nano::uint128_t balance = ledger.balance (transaction, block_a.hashables.previous).value_or (0);
		auto is_send (block_a.hashables.balance < balance);
		nano::account representative{};
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation & add in amount delta
			auto block (ledger.store.block.get (transaction, rep_block_hash));
			debug_assert (block != nullptr);
			representative = block->representative_field ().value ();
			ledger.cache.rep_weights.representation_add_dual (representative, balance, block_a.hashables.representative, 0 - block_a.hashables.balance.number ());
		}
		else
		{
			// Add in amount delta only
			ledger.cache.rep_weights.representation_add (block_a.hashables.representative, 0 - block_a.hashables.balance.number ());
		}

		auto info = ledger.account_info (transaction, block_a.hashables.account);
		debug_assert (info);

		if (is_send)
		{
			nano::pending_key key (block_a.hashables.link.as_account (), hash);
			while (!error && !ledger.store.pending.exists (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link.as_account ()), list);
			}
			ledger.store.pending.del (transaction, key);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
			auto source_account = ledger.account (transaction, block_a.hashables.link.as_block_hash ());
			nano::pending_info pending_info (source_account.value_or (0), block_a.hashables.balance.number () - balance, block_a.sideband ().source_epoch);
			ledger.store.pending.put (transaction, nano::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()), pending_info);
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::receive);
		}

		debug_assert (!error);
		auto previous_version (ledger.version (transaction, block_a.hashables.previous));
		nano::account_info new_info (block_a.hashables.previous, representative, info->open_block, balance, nano::seconds_since_epoch (), info->block_count - 1, previous_version);
		ledger.update_account (transaction, block_a.hashables.account, *info, new_info);

		auto previous (ledger.store.block.get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block.successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < nano::block_type::state)
			{
				ledger.store.frontier.put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (nano::stat::type::rollback, nano::stat::detail::open);
		}
		ledger.store.block.del (transaction, hash);
	}
	nano::store::write_transaction const & transaction;
	nano::ledger & ledger;
	std::vector<std::shared_ptr<nano::block>> & list;
	bool error{ false };
};

class ledger_processor : public nano::mutable_block_visitor
{
public:
	ledger_processor (nano::ledger &, nano::store::write_transaction const &);
	virtual ~ledger_processor () = default;
	void send_block (nano::send_block &) override;
	void receive_block (nano::receive_block &) override;
	void open_block (nano::open_block &) override;
	void change_block (nano::change_block &) override;
	void state_block (nano::state_block &) override;
	void state_block_impl (nano::state_block &);
	void epoch_block_impl (nano::state_block &);
	nano::ledger & ledger;
	nano::store::write_transaction const & transaction;
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
			prev_balance = ledger.balance (transaction, block_a.hashables.previous).value ();
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
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
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
							result = ledger.block_or_pruned_exists (transaction, block_a.hashables.link.as_block_hash ()) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block already? (Harmless)
							if (result == nano::block_status::progress)
							{
								nano::pending_key key (block_a.hashables.account, block_a.hashables.link.as_block_hash ());
								nano::pending_info pending;
								result = ledger.store.pending.get (transaction, key, pending) ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
								if (result == nano::block_status::progress)
								{
									result = amount == pending.amount ? nano::block_status::progress : nano::block_status::balance_mismatch;
									source_epoch = pending.epoch;
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
							ledger.cache.rep_weights.representation_add_dual (info.representative, 0 - info.balance.number (), block_a.hashables.representative, block_a.hashables.balance.number ());
						}
						else
						{
							// Add in amount delta only
							ledger.cache.rep_weights.representation_add (block_a.hashables.representative, block_a.hashables.balance.number ());
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
						if (!ledger.store.frontier.get (transaction, info.head).is_zero ())
						{
							ledger.store.frontier.del (transaction, info.head);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (nano::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
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
						bool pending_exists = ledger.store.pending.any (transaction, block_a.hashables.account);
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
								if (!ledger.store.frontier.get (transaction, info.head).is_zero ())
								{
									ledger.store.frontier.del (transaction, info.head);
								}
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
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
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
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result = account.is_zero () ? nano::block_status::fork : nano::block_status::progress;
				if (result == nano::block_status::progress)
				{
					auto info = ledger.account_info (transaction, account);
					debug_assert (info);
					debug_assert (info->head == block_a.hashables.previous);
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
							ledger.cache.rep_weights.representation_add_dual (block_a.hashables.representative, balance.number (), info->representative, 0 - balance.number ());
							nano::account_info new_info (hash, block_a.hashables.representative, info->open_block, info->balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
							ledger.update_account (transaction, account, *info, new_info);
							ledger.store.frontier.del (transaction, block_a.hashables.previous);
							ledger.store.frontier.put (transaction, hash, account);
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
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
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
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result = account.is_zero () ? nano::block_status::fork : nano::block_status::progress;
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
							auto info = ledger.account_info (transaction, account);
							debug_assert (info);
							debug_assert (info->head == block_a.hashables.previous);
							result = info->balance.number () >= block_a.hashables.balance.number () ? nano::block_status::progress : nano::block_status::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result == nano::block_status::progress)
							{
								auto amount (info->balance.number () - block_a.hashables.balance.number ());
								ledger.cache.rep_weights.representation_add (info->representative, 0 - amount);
								block_a.sideband_set (nano::block_sideband (account, 0, block_a.hashables.balance /* unused */, info->block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
								ledger.store.block.put (transaction, hash, block_a);
								nano::account_info new_info (hash, info->representative, info->open_block, block_a.hashables.balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
								ledger.update_account (transaction, account, *info, new_info);
								ledger.store.pending.put (transaction, nano::pending_key (block_a.hashables.destination, hash), { account, amount, nano::epoch::epoch_0 });
								ledger.store.frontier.del (transaction, block_a.hashables.previous);
								ledger.store.frontier.put (transaction, hash, account);
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
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
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
				auto account (ledger.store.frontier.get (transaction, block_a.hashables.previous));
				result = account.is_zero () ? nano::block_status::gap_previous : nano::block_status::progress; // Have we seen the previous block? No entries for account at all (Harmless)
				if (result == nano::block_status::progress)
				{
					result = validate_message (account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is the signature valid (Malformed)
					if (result == nano::block_status::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block already? (Harmless)
						if (result == nano::block_status::progress)
						{
							auto info = ledger.account_info (transaction, account);
							debug_assert (info);
							result = info->head == block_a.hashables.previous ? nano::block_status::progress : nano::block_status::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result == nano::block_status::progress)
							{
								nano::pending_key key (account, block_a.hashables.source);
								nano::pending_info pending;
								result = ledger.store.pending.get (transaction, key, pending) ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
								if (result == nano::block_status::progress)
								{
									result = pending.epoch == nano::epoch::epoch_0 ? nano::block_status::progress : nano::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result == nano::block_status::progress)
									{
										nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result == nano::block_status::progress)
										{
											auto new_balance (info->balance.number () + pending.amount.number ());
#ifdef NDEBUG
											if (ledger.store.block.exists (transaction, block_a.hashables.source))
											{
												auto info = ledger.account_info (transaction, pending.source);
												debug_assert (info);
											}
#endif
											ledger.store.pending.del (transaction, key);
											block_a.sideband_set (nano::block_sideband (account, 0, new_balance, info->block_count + 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
											ledger.store.block.put (transaction, hash, block_a);
											nano::account_info new_info (hash, info->representative, info->open_block, new_balance, nano::seconds_since_epoch (), info->block_count + 1, nano::epoch::epoch_0);
											ledger.update_account (transaction, account, *info, new_info);
											ledger.cache.rep_weights.representation_add (info->representative, pending.amount.number ());
											ledger.store.frontier.del (transaction, block_a.hashables.previous);
											ledger.store.frontier.put (transaction, hash, account);
											ledger.stats.inc (nano::stat::type::ledger, nano::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
				else
				{
					result = ledger.store.block.exists (transaction, block_a.hashables.previous) ? nano::block_status::fork : nano::block_status::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (nano::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result = existing ? nano::block_status::old : nano::block_status::progress; // Have we seen this block already? (Harmless)
	if (result == nano::block_status::progress)
	{
		result = validate_message (block_a.hashables.account, hash, block_a.signature) ? nano::block_status::bad_signature : nano::block_status::progress; // Is the signature valid (Malformed)
		if (result == nano::block_status::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? nano::block_status::progress : nano::block_status::gap_source; // Have we seen the source block? (Harmless)
			if (result == nano::block_status::progress)
			{
				nano::account_info info;
				result = ledger.store.account.get (transaction, block_a.hashables.account, info) ? nano::block_status::progress : nano::block_status::fork; // Has this account already been opened? (Malicious)
				if (result == nano::block_status::progress)
				{
					nano::pending_key key (block_a.hashables.account, block_a.hashables.source);
					nano::pending_info pending;
					result = ledger.store.pending.get (transaction, key, pending) ? nano::block_status::unreceivable : nano::block_status::progress; // Has this source already been received (Malformed)
					if (result == nano::block_status::progress)
					{
						result = block_a.hashables.account == ledger.constants.burn_account ? nano::block_status::opened_burn_account : nano::block_status::progress; // Is it burning 0 account? (Malicious)
						if (result == nano::block_status::progress)
						{
							result = pending.epoch == nano::epoch::epoch_0 ? nano::block_status::progress : nano::block_status::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result == nano::block_status::progress)
							{
								nano::block_details block_details (nano::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result = ledger.constants.work.difficulty (block_a) >= ledger.constants.work.threshold (block_a.work_version (), block_details) ? nano::block_status::progress : nano::block_status::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result == nano::block_status::progress)
								{
#ifdef NDEBUG
									if (ledger.store.block.exists (transaction, block_a.hashables.source))
									{
										nano::account_info source_info;
										[[maybe_unused]] auto error (ledger.store.account.get (transaction, pending.source, source_info));
										debug_assert (!error);
									}
#endif
									ledger.store.pending.del (transaction, key);
									block_a.sideband_set (nano::block_sideband (block_a.hashables.account, 0, pending.amount, 1, nano::seconds_since_epoch (), block_details, nano::epoch::epoch_0 /* unused */));
									ledger.store.block.put (transaction, hash, block_a);
									nano::account_info new_info (hash, block_a.representative_field ().value (), hash, pending.amount.number (), nano::seconds_since_epoch (), 1, nano::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.cache.rep_weights.representation_add (block_a.representative_field ().value (), pending.amount.number ());
									ledger.store.frontier.put (transaction, hash, block_a.hashables.account);
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

ledger_processor::ledger_processor (nano::ledger & ledger_a, nano::store::write_transaction const & transaction_a) :
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
	representative_visitor (nano::store::transaction const & transaction_a, nano::ledger & ledger);
	~representative_visitor () = default;
	void compute (nano::block_hash const & hash_a);
	void send_block (nano::send_block const & block_a) override;
	void receive_block (nano::receive_block const & block_a) override;
	void open_block (nano::open_block const & block_a) override;
	void change_block (nano::change_block const & block_a) override;
	void state_block (nano::state_block const & block_a) override;
	nano::store::transaction const & transaction;
	nano::ledger & ledger;
	nano::block_hash current;
	nano::block_hash result;
};

representative_visitor::representative_visitor (nano::store::transaction const & transaction_a, nano::ledger & ledger) :
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
		auto block_l = ledger.block (transaction, current);
		debug_assert (block_l != nullptr);
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
} // namespace

nano::ledger::ledger (nano::store::component & store_a, nano::stats & stat_a, nano::ledger_constants & constants, nano::generate_cache_flags const & generate_cache_flags_a) :
	constants{ constants },
	store{ store_a },
	stats{ stat_a },
	check_bootstrap_weights{ true }
{
	if (!store.init_error ())
	{
		initialize (generate_cache_flags_a);
	}
}

void nano::ledger::initialize (nano::generate_cache_flags const & generate_cache_flags_a)
{
	if (generate_cache_flags_a.reps || generate_cache_flags_a.account_count || generate_cache_flags_a.block_count)
	{
		store.account.for_each_par (
		[this] (store::read_transaction const & /*unused*/, store::iterator<nano::account, nano::account_info> i, store::iterator<nano::account, nano::account_info> n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			decltype (this->cache.rep_weights) rep_weights_l;
			for (; i != n; ++i)
			{
				nano::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
				rep_weights_l.representation_add (info.representative, info.balance.number ());
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
			this->cache.rep_weights.copy_from (rep_weights_l);
		});
	}

	if (generate_cache_flags_a.cemented_count)
	{
		store.confirmation_height.for_each_par (
		[this] (store::read_transaction const & /*unused*/, store::iterator<nano::account, nano::confirmation_height_info> i, store::iterator<nano::account, nano::confirmation_height_info> n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});
	}

	auto transaction (store.tx_begin_read ());
	cache.pruned_count = store.pruned.count (transaction);

	// Final votes requirement for confirmation canary block
	nano::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height.get (transaction, constants.final_votes_canary_account, confirmation_height_info))
	{
		cache.final_votes_confirmation_canary = (confirmation_height_info.height >= constants.final_votes_canary_height);
	}
}

// Balance for account containing hash
std::optional<nano::uint128_t> nano::ledger::balance (store::transaction const & transaction, nano::block_hash const & hash) const
{
	if (hash.is_zero ())
	{
		return std::nullopt;
	}
	auto block = store.block.get (transaction, hash);
	if (!block)
	{
		return std::nullopt;
	}
	return block->balance ().number ();
}

std::shared_ptr<nano::block> nano::ledger::block (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.block.get (transaction, hash);
}

bool nano::ledger::block_exists (store::transaction const & transaction, nano::block_hash const & hash) const
{
	return store.block.exists (transaction, hash);
}

// Balance for an account by account number
nano::uint128_t nano::ledger::account_balance (store::transaction const & transaction_a, nano::account const & account_a, bool only_confirmed_a)
{
	nano::uint128_t result (0);
	if (only_confirmed_a)
	{
		nano::confirmation_height_info info;
		if (!store.confirmation_height.get (transaction_a, account_a, info))
		{
			result = balance (transaction_a, info.frontier).value ();
		}
	}
	else
	{
		auto info = account_info (transaction_a, account_a);
		if (info)
		{
			result = info->balance.number ();
		}
	}
	return result;
}

nano::uint128_t nano::ledger::account_receivable (store::transaction const & transaction_a, nano::account const & account_a, bool only_confirmed_a)
{
	nano::uint128_t result (0);
	nano::account end (account_a.number () + 1);
	for (auto i (store.pending.begin (transaction_a, nano::pending_key (account_a, 0))), n (store.pending.begin (transaction_a, nano::pending_key (end, 0))); i != n; ++i)
	{
		nano::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (block_confirmed (transaction_a, i->first.hash))
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

std::optional<nano::pending_info> nano::ledger::pending_info (store::transaction const & transaction, nano::pending_key const & key) const
{
	nano::pending_info result;
	if (!store.pending.get (transaction, key, result))
	{
		return result;
	}
	return std::nullopt;
}

nano::block_status nano::ledger::process (store::write_transaction const & transaction_a, std::shared_ptr<nano::block> block_a)
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

nano::block_hash nano::ledger::representative (store::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || block_exists (transaction_a, result));
	return result;
}

nano::block_hash nano::ledger::representative_calculated (store::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, *this);
	visitor.compute (hash_a);
	return visitor.result;
}

bool nano::ledger::block_or_pruned_exists (nano::block_hash const & hash_a) const
{
	return block_or_pruned_exists (store.tx_begin_read (), hash_a);
}

bool nano::ledger::block_or_pruned_exists (store::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	if (store.pruned.exists (transaction_a, hash_a))
	{
		return true;
	}
	return block_exists (transaction_a, hash_a);
}

bool nano::ledger::root_exists (store::transaction const & transaction_a, nano::root const & root_a)
{
	return block_exists (transaction_a, root_a.as_block_hash ()) || store.account.exists (transaction_a, root_a.as_account ());
}

std::string nano::ledger::block_text (char const * hash_a)
{
	return block_text (nano::block_hash (hash_a));
}

std::string nano::ledger::block_text (nano::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block_l = block (transaction, hash_a);
	if (block_l != nullptr)
	{
		block_l->serialize_json (result);
	}
	return result;
}

nano::account const & nano::ledger::block_destination (store::transaction const & transaction_a, nano::block const & block_a)
{
	nano::send_block const * send_block (dynamic_cast<nano::send_block const *> (&block_a));
	nano::state_block const * state_block (dynamic_cast<nano::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		return send_block->hashables.destination;
	}
	else if (state_block != nullptr && block_a.is_send ())
	{
		return state_block->hashables.link.as_account ();
	}

	return nano::account::null ();
}

std::pair<nano::block_hash, nano::block_hash> nano::ledger::hash_root_random (store::transaction const & transaction_a) const
{
	nano::block_hash hash (0);
	nano::root root (0);
	if (!pruning)
	{
		auto block (store.block.random (transaction_a));
		hash = block->hash ();
		root = block->root ();
	}
	else
	{
		uint64_t count (cache.block_count);
		auto region = nano::random_pool::generate_word64 (0, count - 1);
		// Pruned cache cannot guarantee that pruned blocks are already commited
		if (region < cache.pruned_count)
		{
			hash = store.pruned.random (transaction_a);
		}
		if (hash.is_zero ())
		{
			auto block (store.block.random (transaction_a));
			hash = block->hash ();
			root = block->root ();
		}
	}
	return std::make_pair (hash, root.as_block_hash ());
}

// Vote weight of an account
nano::uint128_t nano::ledger::weight (nano::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		if (cache.block_count < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return cache.rep_weights.representation_get (account_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool nano::ledger::rollback (store::write_transaction const & transaction_a, nano::block_hash const & block_a, std::vector<std::shared_ptr<nano::block>> & list_a)
{
	debug_assert (block_exists (transaction_a, block_a));
	auto account_l = account (transaction_a, block_a).value ();
	auto block_account_height (height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	auto error (false);
	while (!error && block_exists (transaction_a, block_a))
	{
		nano::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto info = account_info (transaction_a, account_l);
			debug_assert (info);
			auto block_l = block (transaction_a, info->head);
			list_a.push_back (block_l);
			block_l->visit (rollback);
			error = rollback.error;
			if (!error)
			{
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

bool nano::ledger::rollback (store::write_transaction const & transaction_a, nano::block_hash const & block_a)
{
	std::vector<std::shared_ptr<nano::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

std::optional<nano::account> nano::ledger::account (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = block (transaction, hash);
	if (!block_l)
	{
		return std::nullopt;
	}
	return block_l->account ();
}

std::optional<nano::account_info> nano::ledger::account_info (store::transaction const & transaction, nano::account const & account) const
{
	return store.account.get (transaction, account);
}

std::optional<nano::uint128_t> nano::ledger::amount (store::transaction const & transaction_a, nano::block_hash const & hash_a)
{
	auto block_l = block (transaction_a, hash_a);
	if (!block_l)
	{
		return std::nullopt;
	}
	auto block_balance = block_l->balance ();
	if (block_l->previous ().is_zero ())
	{
		return block_balance.number ();
	}
	auto previous_balance = balance (transaction_a, block_l->previous ());
	if (!previous_balance)
	{
		return std::nullopt;
	}
	return block_balance > previous_balance.value () ? block_balance.number () - previous_balance.value () : previous_balance.value () - block_balance.number ();
}

// Return latest block for account
nano::block_hash nano::ledger::latest (store::transaction const & transaction_a, nano::account const & account_a)
{
	auto info = account_info (transaction_a, account_a);
	return !info ? 0 : info->head;
}

// Return latest root for account, account number if there are no blocks for this account.
nano::root nano::ledger::latest_root (store::transaction const & transaction_a, nano::account const & account_a)
{
	auto info = account_info (transaction_a, account_a);
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
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block_l = block (transaction, hash);
		debug_assert (block_l != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block_l->previous ();
	}
}

bool nano::ledger::dependents_confirmed (store::transaction const & transaction_a, nano::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (nano::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = block_confirmed (transaction_a, hash_a);
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
	dependent_block_visitor (nano::ledger const & ledger_a, nano::store::transaction const & transaction_a) :
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
		if (block_a.source_field ().value () != ledger.constants.genesis->account ())
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
		if (ledger.is_epoch_link (block_a.hashables.link) || is_send (transaction, block_a))
		{
			result[1].clear ();
		}
	}
	// This function is used in place of block->is_send () as it is tolerant to the block not having the sideband information loaded
	// This is needed for instance in vote generation on forks which have not yet had sideband information attached
	bool is_send (nano::store::transaction const & transaction, nano::state_block const & block) const
	{
		if (block.previous ().is_zero ())
		{
			return false;
		}
		if (block.has_sideband ())
		{
			return block.sideband ().details.is_send;
		}
		return block.balance_field ().value () < ledger.balance (transaction, block.previous ());
	}
	nano::ledger const & ledger;
	nano::store::transaction const & transaction;
	std::array<nano::block_hash, 2> result;
};

std::array<nano::block_hash, 2> nano::ledger::dependent_blocks (store::transaction const & transaction_a, nano::block const & block_a) const
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

/** Given the block hash of a send block, find the associated receive block that receives that send.
 *  The send block hash is not checked in any way, it is assumed to be correct.
 * @return Return the receive block on success and null on failure
 */
std::shared_ptr<nano::block> nano::ledger::find_receive_block_by_send_hash (store::transaction const & transaction, nano::account const & destination, nano::block_hash const & send_block_hash)
{
	std::shared_ptr<nano::block> result;
	debug_assert (send_block_hash != 0);

	// get the cemented frontier
	nano::confirmation_height_info info;
	if (store.confirmation_height.get (transaction, destination, info))
	{
		return nullptr;
	}
	auto possible_receive_block = block (transaction, info.frontier);

	// walk down the chain until the source field of a receive block matches the send block hash
	while (possible_receive_block != nullptr)
	{
		if (possible_receive_block->is_receive () && send_block_hash == possible_receive_block->source ())
		{
			// we have a match
			result = possible_receive_block;
			break;
		}

		possible_receive_block = block (transaction, possible_receive_block->previous ());
	}

	return result;
}

nano::account const & nano::ledger::epoch_signer (nano::link const & link_a) const
{
	return constants.epochs.signer (constants.epochs.epoch (link_a));
}

nano::link const & nano::ledger::epoch_link (nano::epoch epoch_a) const
{
	return constants.epochs.link (epoch_a);
}

void nano::ledger::update_account (store::write_transaction const & transaction_a, nano::account const & account_a, nano::account_info const & old_a, nano::account_info const & new_a)
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
		debug_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<nano::block> nano::ledger::successor (store::transaction const & transaction_a, nano::qualified_root const & root_a)
{
	nano::block_hash successor (0);
	auto get_from_previous = false;
	if (root_a.previous ().is_zero ())
	{
		auto info = account_info (transaction_a, root_a.root ().as_account ());
		if (info)
		{
			successor = info->open_block;
		}
		else
		{
			get_from_previous = true;
		}
	}
	else
	{
		get_from_previous = true;
	}

	if (get_from_previous)
	{
		successor = store.block.successor (transaction_a, root_a.previous ());
	}
	std::shared_ptr<nano::block> result;
	if (!successor.is_zero ())
	{
		result = block (transaction_a, successor);
	}
	debug_assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<nano::block> nano::ledger::forked_block (store::transaction const & transaction_a, nano::block const & block_a)
{
	debug_assert (!block_exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (block_exists (transaction_a, root.as_block_hash ()) || store.account.exists (transaction_a, root.as_account ()));
	auto result = block (transaction_a, store.block.successor (transaction_a, root.as_block_hash ()));
	if (result == nullptr)
	{
		auto info = account_info (transaction_a, root.as_account ());
		debug_assert (info);
		result = block (transaction_a, info->open_block);
		debug_assert (result != nullptr);
	}
	return result;
}

std::shared_ptr<nano::block> nano::ledger::head_block (store::transaction const & transaction, nano::account const & account)
{
	auto info = store.account.get (transaction, account);
	if (info)
	{
		return block (transaction, info->head);
	}
	return nullptr;
}

bool nano::ledger::block_confirmed (store::transaction const & transaction_a, nano::block_hash const & hash_a) const
{
	if (store.pruned.exists (transaction_a, hash_a))
	{
		return true;
	}
	auto block_l = block (transaction_a, hash_a);
	if (block_l)
	{
		nano::confirmation_height_info confirmation_height_info;
		store.confirmation_height.get (transaction_a, block_l->account (), confirmation_height_info);
		auto confirmed (confirmation_height_info.height >= block_l->sideband ().height);
		return confirmed;
	}
	return false;
}

uint64_t nano::ledger::pruning_action (store::write_transaction & transaction_a, nano::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	nano::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != constants.genesis->hash ())
	{
		auto block_l = block (transaction_a, hash);
		if (block_l != nullptr)
		{
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
			release_assert (false && "Error finding block for pruning");
		}
	}
	return pruned_count;
}

std::multimap<uint64_t, nano::uncemented_info, std::greater<>> nano::ledger::unconfirmed_frontiers () const
{
	nano::locked<std::multimap<uint64_t, nano::uncemented_info, std::greater<>>> result;
	using result_t = decltype (result)::value_type;

	store.account.for_each_par ([this, &result] (store::read_transaction const & transaction_a, store::iterator<nano::account, nano::account_info> i, store::iterator<nano::account, nano::account_info> n) {
		result_t unconfirmed_frontiers_l;
		for (; i != n; ++i)
		{
			auto const & account (i->first);
			auto const & account_info (i->second);

			nano::confirmation_height_info conf_height_info;
			this->store.confirmation_height.get (transaction_a, account, conf_height_info);

			if (account_info.block_count != conf_height_info.height)
			{
				// Always output as no confirmation height has been set on the account yet
				auto height_delta = account_info.block_count - conf_height_info.height;
				auto const & frontier = account_info.head;
				auto const & cemented_frontier = conf_height_info.frontier;
				unconfirmed_frontiers_l.emplace (std::piecewise_construct, std::forward_as_tuple (height_delta), std::forward_as_tuple (cemented_frontier, frontier, i->first));
			}
		}
		// Merge results
		auto result_locked = result.lock ();
		result_locked->insert (unconfirmed_frontiers_l.begin (), unconfirmed_frontiers_l.end ());
	});
	return result;
}

// A precondition is that the store is an LMDB store
bool nano::ledger::migrate_lmdb_to_rocksdb (std::filesystem::path const & data_path_a) const
{
	boost::system::error_code error_chmod;
	nano::set_secure_perm_directory (data_path_a, error_chmod);
	auto rockdb_data_path = data_path_a / "rocksdb";
	std::filesystem::remove_all (rockdb_data_path);

	nano::logger logger;
	auto error (false);

	// Open rocksdb database
	nano::rocksdb_config rocksdb_config;
	rocksdb_config.enable = true;
	auto rocksdb_store = nano::make_store (logger, data_path_a, nano::dev::constants, false, true, rocksdb_config);

	if (!rocksdb_store->init_error ())
	{
		store.block.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::blocks }));

				std::vector<uint8_t> vector;
				{
					nano::vectorstream stream (vector);
					nano::serialize_block (stream, *i->second.block);
					i->second.sideband.serialize (stream, i->second.block->type ());
				}
				rocksdb_store->block.raw_put (rocksdb_transaction, vector, i->first);
			}
		});

		store.pending.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::pending }));
				rocksdb_store->pending.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.confirmation_height.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::confirmation_height }));
				rocksdb_store->confirmation_height.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.account.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::accounts }));
				rocksdb_store->account.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.frontier.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::frontiers }));
				rocksdb_store->frontier.put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.pruned.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::pruned }));
				rocksdb_store->pruned.put (rocksdb_transaction, i->first);
			}
		});

		store.final_vote.for_each_par (
		[&rocksdb_store] (store::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { nano::tables::final_votes }));
				rocksdb_store->final_vote.put (rocksdb_transaction, i->first, i->second);
			}
		});

		auto lmdb_transaction (store.tx_begin_read ());
		auto version = store.version.get (lmdb_transaction);
		auto rocksdb_transaction (rocksdb_store->tx_begin_write ());
		rocksdb_store->version.put (rocksdb_transaction, version);

		for (auto i (store.online_weight.begin (lmdb_transaction)), n (store.online_weight.end ()); i != n; ++i)
		{
			rocksdb_store->online_weight.put (rocksdb_transaction, i->first, i->second);
		}

		for (auto i (store.peer.begin (lmdb_transaction)), n (store.peer.end ()); i != n; ++i)
		{
			rocksdb_store->peer.put (rocksdb_transaction, i->first);
		}

		// Compare counts
		error |= store.peer.count (lmdb_transaction) != rocksdb_store->peer.count (rocksdb_transaction);
		error |= store.pruned.count (lmdb_transaction) != rocksdb_store->pruned.count (rocksdb_transaction);
		error |= store.final_vote.count (lmdb_transaction) != rocksdb_store->final_vote.count (rocksdb_transaction);
		error |= store.online_weight.count (lmdb_transaction) != rocksdb_store->online_weight.count (rocksdb_transaction);
		error |= store.version.get (lmdb_transaction) != rocksdb_store->version.get (rocksdb_transaction);

		// For large tables a random key is used instead and makes sure it exists
		auto random_block (store.block.random (lmdb_transaction));
		error |= rocksdb_store->block.get (rocksdb_transaction, random_block->hash ()) == nullptr;

		auto account = random_block->account ();
		nano::account_info account_info;
		error |= rocksdb_store->account.get (rocksdb_transaction, account, account_info);

		// If confirmation height exists in the lmdb ledger for this account it should exist in the rocksdb ledger
		nano::confirmation_height_info confirmation_height_info{};
		if (!store.confirmation_height.get (lmdb_transaction, account, confirmation_height_info))
		{
			error |= rocksdb_store->confirmation_height.get (rocksdb_transaction, account, confirmation_height_info);
		}
	}
	else
	{
		error = true;
	}
	return error;
}

bool nano::ledger::bootstrap_weight_reached () const
{
	return cache.block_count >= bootstrap_weight_max_blocks;
}

nano::epoch nano::ledger::version (nano::block const & block)
{
	if (block.type () == nano::block_type::state)
	{
		return block.sideband ().details.epoch;
	}

	return nano::epoch::epoch_0;
}

nano::epoch nano::ledger::version (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = block (transaction, hash);
	if (block_l == nullptr)
	{
		return nano::epoch::epoch_0;
	}
	return version (*block_l);
}

uint64_t nano::ledger::height (store::transaction const & transaction, nano::block_hash const & hash) const
{
	auto block_l = block (transaction, hash);
	return block_l->sideband ().height;
}

nano::uncemented_info::uncemented_info (nano::block_hash const & cemented_frontier, nano::block_hash const & frontier, nano::account const & account) :
	cemented_frontier (cemented_frontier), frontier (frontier), account (account)
{
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (ledger & ledger, std::string const & name)
{
	auto count = ledger.bootstrap_weights.size ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (collect_container_info (ledger.cache.rep_weights, "rep_weights"));
	return composite;
}
