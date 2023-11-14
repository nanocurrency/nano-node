#include <nano/lib/blocks.hpp>
#include <nano/secure/block_check_context.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>

nano::block_check_context::block_check_context (nano::ledger & ledger, std::shared_ptr<nano::block> block) :
	block_m{ block },
	ledger{ ledger }
{
}

auto nano::block_check_context::op () const -> block_op
{
	debug_assert (state.has_value ());
	switch (block_m->type ())
	{
		case nano::block_type::state:
			if (block_m->balance_field ().value () < state->balance)
			{
				return block_op::send;
			}
			if (previous != nullptr && block_m->link_field ().value ().is_zero ())
			{
				return block_op::noop;
			}
			if (ledger.constants.epochs.is_epoch_link (block_m->link_field ().value ()))
			{
				return block_op::epoch;
			}
			return block_op::receive;
		case nano::block_type::send:
			return block_op::send;
		case nano::block_type::open:
		case nano::block_type::receive:
			return block_op::receive;
		case nano::block_type::change:
			return block_op::noop;
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			release_assert (false);
			break;
	}
	release_assert (false);
}

bool nano::block_check_context::is_send () const
{
	return op () == block_op::send;
}

bool nano::block_check_context::is_receive () const
{
	return op () == block_op::receive;
}

bool nano::block_check_context::is_epoch () const
{
	return op () == block_op::epoch;
}

nano::amount nano::block_check_context::balance () const
{
	switch (block_m->type ())
	{
		case nano::block_type::state:
		case nano::block_type::send:
			return block_m->balance_field ().value ();
		case nano::block_type::open:
			return receivable->amount;
		case nano::block_type::change:
			return previous->balance ();
		case nano::block_type::receive:
			return previous->balance ().number () + receivable->amount.number ();
		default:
			release_assert (false);
	}
}

uint64_t nano::block_check_context::height () const
{
	return previous ? previous->sideband ().height + 1 : 1;
}

nano::epoch nano::block_check_context::epoch () const
{
	if (is_epoch ())
	{
		return ledger.constants.epochs.epoch (block_m->link_field ().value ());
	}
	nano::epoch account_epoch{ nano::epoch::epoch_0 };
	nano::epoch source_epoch{ nano::epoch::epoch_0 };
	if (previous != nullptr)
	{
		account_epoch = previous->sideband ().details.epoch;
	}
	if (receivable.has_value ())
	{
		source_epoch = receivable->epoch;
	}
	return std::max (account_epoch, source_epoch);
}

nano::amount nano::block_check_context::amount () const
{
	auto balance_l = balance ();
	auto previous_balance = previous ? previous->balance () : 0;
	switch (op ())
	{
		case block_op::receive:
			return balance_l.number () - previous_balance.number ();
		case block_op::send:
			return previous_balance.number () - balance_l.number ();
		case block_op::epoch:
		case block_op::noop:
			release_assert (balance_l.number () == previous_balance.number ());
			return 0;
	}
}

nano::account nano::block_check_context::representative () const
{
	switch (block_m->type ())
	{
		case nano::block_type::state:
		case nano::block_type::open:
		case nano::block_type::change:
			return block_m->representative_field ().value ();
		case nano::block_type::send:
		case nano::block_type::receive:
			return state->representative;
		default:
			release_assert (false);
	}
}

nano::block_hash nano::block_check_context::open () const
{
	if (previous == nullptr)
	{
		return block_m->hash ();
	}
	return state->open_block;
}

bool nano::block_check_context::old () const
{
	return block_m == nullptr;
}

nano::account nano::block_check_context::account () const
{
	switch (block_m->type ())
	{
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			debug_assert (previous != nullptr);
			switch (previous->type ())
			{
				case nano::block_type::state:
				case nano::block_type::open:
					return previous->account ();
				case nano::block_type::change:
				case nano::block_type::receive:
				case nano::block_type::send:
					return previous->sideband ().account;
				case nano::block_type::not_a_block:
				case nano::block_type::invalid:
					debug_assert (false);
					break;
			}
			break;
		case nano::block_type::state:
		case nano::block_type::open:
			return block_m->account_field ().value ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return 1; // Return an account that cannot be signed for.
}

nano::block_hash nano::block_check_context::source () const
{
	switch (block_m->type ())
	{
		case nano::block_type::send:
		case nano::block_type::change:
			// 0 is returned for source on send/change blocks
			return 0;
		case nano::block_type::receive:
		case nano::block_type::open:
			return block_m->source_field ().value ();
		case nano::block_type::state:
			return block_m->link_field ().value ().as_block_hash ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			return 0;
	}
	debug_assert (false);
	return 0;
}

nano::account nano::block_check_context::signer (nano::epochs const & epochs) const
{
	debug_assert (block_m != nullptr);
	switch (block_m->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
			debug_assert (previous != nullptr); // Previous block must be passed in for non-open blocks
			switch (previous->type ())
			{
				case nano::block_type::state:
					debug_assert (false && "Legacy blocks can't follow state blocks");
					break;
				case nano::block_type::open:
					// Open blocks have the account written in the block.
					return previous->account ();
				default:
					// Other legacy block types have the account stored in sideband.
					return previous->sideband ().account;
			}
			break;
		case nano::block_type::state:
		{
			debug_assert (dynamic_cast<nano::state_block *> (block_m.get ()));
			// If the block is a send, while the link field may contain an epoch link value, it is actually a malformed destination address.
			return (!epochs.is_epoch_link (block_m->link_field ().value ()) || is_send ()) ? block_m->account_field ().value () : epochs.signer (epochs.epoch (block_m->link_field ().value ()));
		}
		case nano::block_type::open: // Open block signer is determined statelessly as it's written in the block
			return block_m->account_field ().value ();
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return 1; // Return an account that cannot be signed for.
}

bool nano::block_check_context::gap_previous () const
{
	return !block_m->previous ().is_zero () && previous == nullptr;
}

bool nano::block_check_context::failed (nano::block_status const & code) const
{
	return code != nano::block_status::progress;
}

nano::block_status nano::block_check_context::rule_sufficient_work () const
{
	if (ledger.constants.work.difficulty (*block_m) < ledger.constants.work.threshold (block_m->work_version (), details))
	{
		return nano::block_status::insufficient_work;
	}
	return nano::block_status::progress;
}

nano::block_status nano::block_check_context::rule_reserved_account () const
{
	switch (block_m->type ())
	{
		case nano::block_type::open:
		case nano::block_type::state:
			if (!block_m->account_field ().value ().is_zero ())
			{
				return nano::block_status::progress;
			}
			else
			{
				return nano::block_status::opened_burn_account;
			}
			break;
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			return nano::block_status::progress;
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			release_assert (false);
			break;
	}
	release_assert (false);
}

nano::block_status nano::block_check_context::rule_previous_frontier () const
{
	debug_assert (block_m != nullptr); //
	if (gap_previous ())
	{
		return nano::block_status::gap_previous;
	}
	else
	{
		return nano::block_status::progress;
	}
}

nano::block_status nano::block_check_context::rule_state_block_account_position () const
{
	if (previous == nullptr)
	{
		return nano::block_status::progress;
	}
	switch (block_m->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
		{
			switch (previous->type ())
			{
				case nano::block_type::state:
					return nano::block_status::block_position;
				default:
					return nano::block_status::progress;
			}
		}
		default:
			return nano::block_status::progress;
	}
}

nano::block_status nano::block_check_context::rule_state_block_source_position () const
{
	if (!receivable.has_value ())
	{
		return nano::block_status::progress;
	}
	switch (block_m->type ())
	{
		case nano::block_type::receive:
		case nano::block_type::open:
		{
			if (receivable->epoch > nano::epoch::epoch_0)
			{
				return nano::block_status::unreceivable;
			}
			return nano::block_status::progress;
		}
		case nano::block_type::state:
			return nano::block_status::progress;
		default:
			release_assert (false);
	}
}

nano::block_status nano::block_check_context::rule_block_signed () const
{
	if (!nano::validate_message (signer (ledger.constants.epochs), block_m->hash (), block_m->block_signature ()))
	{
		return nano::block_status::progress;
	}
	return nano::block_status::bad_signature;
}

nano::block_status nano::block_check_context::rule_metastable () const
{
	debug_assert (state.has_value ());
	if (block_m->previous () == state->head)
	{
		return nano::block_status::progress;
	}
	else
	{
		return nano::block_status::fork;
	}
}

nano::block_status nano::block_check_context::check_receive_rules () const
{
	if (!source_exists)
	{
		// Probably redundant to check as receivable would also have no value
		return nano::block_status::gap_source;
	}
	if (!receivable.has_value ())
	{
		return nano::block_status::unreceivable;
	}
	if (block_m->type () == nano::block_type::state)
	{
		auto next_balance = state->balance.number () + receivable->amount.number ();
		if (next_balance != balance ().number ())
		{
			return nano::block_status::balance_mismatch;
		}
	}
	return nano::block_status::progress;
}

nano::block_status nano::block_check_context::check_epoch_rules () const
{
	debug_assert (state.has_value ());
	// Epoch blocks may not change an account's balance
	if (state->balance != balance ())
	{
		return nano::block_status::balance_mismatch;
	}
	// Epoch blocks may not change an account's representative
	if (state->representative != representative ())
	{
		return nano::block_status::representative_mismatch;
	}
	// Epoch blocks may not be created for accounts that have no receivable entries
	if (block_m->previous ().is_zero () && !any_receivable)
	{
		return nano::block_status::gap_epoch_open_pending;
	}
	auto previous_epoch = nano::epoch::epoch_0;
	if (previous != nullptr)
	{
		previous_epoch = previous->sideband ().details.epoch;
	}
	// Epoch blocks may only increase epoch number by one
	if (!state->head.is_zero () && !nano::epochs::is_sequential (previous_epoch, epoch ()))
	{
		return nano::block_status::block_position;
	}
	return nano::block_status::progress;
}

nano::block_status nano::block_check_context::check_send_rules () const
{
	debug_assert (block_m->type () == nano::block_type::send || block_m->type () == nano::block_type::state);
	if (state->balance < balance ())
	{
		return nano::block_status::negative_spend;
	}
	return nano::block_status::progress;
}

nano::block_status nano::block_check_context::check_noop_rules () const
{
	if (balance () != previous->balance ())
	{
		return nano::block_status::balance_mismatch;
	}
	return nano::block_status::progress;
}

void nano::block_check_context::load (secure::transaction const & transaction)
{
	auto hash = block_m->hash ();
	if (ledger.any.block_exists_or_pruned (transaction, hash))
	{
		block_m = nullptr; // Signal this block already exists by nulling out block
		return;
	}
	auto & block = *block_m;
	if (!block.previous ().is_zero ())
	{
		previous = ledger.any.block_get (transaction, block.previous ());
	}
	if (!gap_previous ())
	{
		auto account_l = account ();
		auto source_l = source ();
		state = ledger.any.account_get (transaction, account_l);
		if (!state)
		{
			state = nano::account_info{};
		}
		source_exists = ledger.any.block_exists_or_pruned (transaction, source_l);
		nano::pending_key key{ account_l, source_l };
		receivable = ledger.any.pending_get (transaction, key);
		any_receivable = ledger.any.receivable_exists (transaction, account_l);
		details = block_details{ epoch (), is_send (), is_receive (), is_epoch () };
	}
}

nano::block_status nano::block_check_context::check (secure::transaction const & transaction)
{
	load (transaction);
	if (old ())
	{
		return nano::block_status::old;
	}
	nano::block_status result;
	if (failed (result = rule_sufficient_work ()))
	{
		return result;
	}
	if (failed (result = rule_reserved_account ()))
	{
		return result;
	}
	if (failed (result = rule_previous_frontier ()))
	{
		return result;
	}
	if (failed (result = rule_state_block_account_position ()))
	{
		return result;
	}
	if (failed (result = rule_state_block_source_position ()))
	{
		return result;
	}
	if (failed (result = rule_block_signed ()))
	{
		return result;
	}
	if (failed (result = rule_metastable ()))
	{
		return result;
	}
	switch (op ())
	{
		case block_op::receive:
			result = check_receive_rules ();
			break;
		case block_op::send:
			result = check_send_rules ();
			break;
		case block_op::noop:
			result = check_noop_rules ();
			break;
		case block_op::epoch:
			result = check_epoch_rules ();
			break;
	}
	if (result == nano::block_status::progress)
	{
		nano::block_sideband sideband{ account (), 0, balance (), height (), nano::seconds_since_epoch (), details, receivable ? receivable->epoch : nano::epoch::epoch_0 };
		block_m->sideband_set (sideband);
		std::pair<std::optional<nano::pending_key>, std::optional<nano::pending_info>> receivable;
		if (is_send ())
		{
			receivable.first = { block_m->destination (), block_m->hash () };
			receivable.second = { account (), amount (), epoch () };
		}
		else if (is_receive ())
		{
			receivable.first = { block_m->account (), block_m->source () };
		}
		std::pair<std::optional<nano::account>, std::optional<nano::amount>> weight;
		if (previous != nullptr)
		{
			weight.first = state->representative;
			weight.second = state->balance;
		}
		nano::account_info info{ block_m->hash (), representative (), open (), balance (), nano::seconds_since_epoch (), height (), epoch () };
		delta = { block_m, info, receivable, weight };
	}
	return result;
}
