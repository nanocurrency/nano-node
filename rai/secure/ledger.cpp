#include <rai/node/common.hpp>
#include <rai/node/stats.hpp>
#include <rai/secure/blockstore.hpp>
#include <rai/secure/ledger.hpp>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public rai::block_visitor
{
public:
	rollback_visitor (rai::transaction const & transaction_a, rai::ledger & ledger_a) :
	transaction (transaction_a),
	ledger (ledger_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (rai::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		rai::pending_info pending;
		rai::pending_key key (block_a.hashables.destination, hash);
		while (ledger.store.pending_get (transaction, key, pending))
		{
			ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination));
		}
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, pending.source, info));
		assert (!error);
		ledger.store.pending_del (transaction, key);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), pending.amount.number ());
		ledger.change_latest (transaction, pending.source, block_a.hashables.previous, info.rep_block, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.block_del (transaction, hash);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
		ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::send);
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, destination_account, info));
		assert (!error);
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, block_a.hashables.previous, representative, ledger.balance (transaction, block_a.hashables.previous), info.block_count - 1);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, rai::pending_key (destination_account, block_a.hashables.source), { source_account, amount, rai::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
		ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::receive);
	}
	void open_block (rai::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, block_a.hashables.source));
		auto destination_account (ledger.account (transaction, hash));
		auto source_account (ledger.account (transaction, block_a.hashables.source));
		ledger.store.representation_add (transaction, ledger.representative (transaction, hash), 0 - amount);
		ledger.change_latest (transaction, destination_account, 0, 0, 0, 0);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, rai::pending_key (destination_account, block_a.hashables.source), { source_account, amount, rai::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::open);
	}
	void change_block (rai::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto representative (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		rai::account_info info;
		auto error (ledger.store.account_get (transaction, account, info));
		assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		ledger.store.representation_add (transaction, representative, balance);
		ledger.store.representation_add (transaction, hash, 0 - balance);
		ledger.store.block_del (transaction, hash);
		ledger.change_latest (transaction, account, block_a.hashables.previous, representative, info.balance, info.block_count - 1);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		if (!(info.block_count % ledger.store.block_info_max))
		{
			ledger.store.block_info_del (transaction, hash);
		}
		ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::change);
	}
	void state_block (rai::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		rai::block_hash representative (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			representative = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		// Add in amount delta
		ledger.store.representation_add (transaction, hash, 0 - block_a.hashables.balance.number ());
		if (!representative.is_zero ())
		{
			// Move existing representation
			ledger.store.representation_add (transaction, representative, balance);
		}

		rai::account_info info;
		auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			rai::pending_key key (block_a.hashables.link, hash);
			while (!ledger.store.pending_exists (transaction, key))
			{
				ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link));
			}
			ledger.store.pending_del (transaction, key);
			ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			auto source_version (ledger.store.block_version (transaction, block_a.hashables.link));
			rai::pending_info pending_info (ledger.account (transaction, block_a.hashables.link), block_a.hashables.balance.number () - balance, source_version);
			ledger.store.pending_put (transaction, rai::pending_key (block_a.hashables.account, block_a.hashables.link), pending_info);
			ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::receive);
		}

		assert (!error);
		auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
		ledger.change_latest (transaction, block_a.hashables.account, block_a.hashables.previous, representative, balance, info.block_count - 1, false, previous_version);

		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < rai::block_type::state)
			{
				ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (rai::stat::type::rollback, rai::stat::detail::open);
		}
		ledger.store.block_del (transaction, hash);
	}
	rai::transaction const & transaction;
	rai::ledger & ledger;
};

class ledger_processor : public rai::block_visitor
{
public:
	ledger_processor (rai::ledger &, rai::transaction const &, bool = false);
	virtual ~ledger_processor () = default;
	void send_block (rai::send_block const &) override;
	void receive_block (rai::receive_block const &) override;
	void open_block (rai::open_block const &) override;
	void change_block (rai::change_block const &) override;
	void state_block (rai::state_block const &) override;
	void state_block_impl (rai::state_block const &);
	void epoch_block_impl (rai::state_block const &);
	rai::ledger & ledger;
	rai::transaction const & transaction;
	bool valid_signature;
	rai::process_return result;
};

void ledger_processor::state_block (rai::state_block const & block_a)
{
	result.code = rai::process_result::progress;
	auto is_epoch_block (false);
	// Check if this is an epoch block
	if (!ledger.epoch_link.is_zero () && ledger.is_epoch_link (block_a.hashables.link))
	{
		rai::amount prev_balance (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? rai::process_result::progress : rai::process_result::gap_previous;
			if (result.code == rai::process_result::progress)
			{
				prev_balance = ledger.balance (transaction, block_a.hashables.previous);
			}
		}
		if (block_a.hashables.balance == prev_balance)
		{
			is_epoch_block = true;
		}
	}
	if (result.code == rai::process_result::progress)
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

void ledger_processor::state_block_impl (rai::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == rai::process_result::progress)
	{
		// Revalidate blocks with epoch links
		if (!valid_signature || ledger.is_epoch_link (block_a.hashables.link))
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == rai::process_result::progress)
		{
			result.code = block_a.hashables.account.is_zero () ? rai::process_result::opened_burn_account : rai::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == rai::process_result::progress)
			{
				rai::epoch epoch (rai::epoch::epoch_0);
				rai::account_info info;
				result.amount = block_a.hashables.balance;
				auto is_send (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					epoch = info.epoch;
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? rai::process_result::fork : rai::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == rai::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? rai::process_result::progress : rai::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == rai::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							result.amount = is_send ? (info.balance.number () - result.amount.number ()) : (result.amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? rai::process_result::progress : rai::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.code = block_a.previous ().is_zero () ? rai::process_result::progress : rai::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == rai::process_result::progress)
					{
						result.code = !block_a.hashables.link.is_zero () ? rai::process_result::progress : rai::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == rai::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = (ledger.store.block_exists (transaction, rai::block_type::state, block_a.hashables.link) || ledger.store.block_exists (transaction, rai::block_type::send, block_a.hashables.link)) ? rai::process_result::progress : rai::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == rai::process_result::progress)
							{
								rai::pending_key key (block_a.hashables.account, block_a.hashables.link);
								rai::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? rai::process_result::unreceivable : rai::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == rai::process_result::progress)
								{
									result.code = result.amount == pending.amount ? rai::process_result::progress : rai::process_result::balance_mismatch;
									epoch = std::max (epoch, pending.epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = result.amount.is_zero () ? rai::process_result::progress : rai::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == rai::process_result::progress)
				{
					ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::state_block);
					result.state_is_send = is_send;
					ledger.store.block_put (transaction, hash, block_a, 0, epoch);

					if (!info.rep_block.is_zero ())
					{
						// Move existing representation
						ledger.store.representation_add (transaction, info.rep_block, 0 - info.balance.number ());
					}
					// Add in amount delta
					ledger.store.representation_add (transaction, hash, block_a.hashables.balance.number ());

					if (is_send)
					{
						rai::pending_key key (block_a.hashables.link, hash);
						rai::pending_info info (block_a.hashables.account, result.amount.number (), epoch);
						ledger.store.pending_put (transaction, key, info);
					}
					else if (!block_a.hashables.link.is_zero ())
					{
						ledger.store.pending_del (transaction, rai::pending_key (block_a.hashables.account, block_a.hashables.link));
					}

					ledger.change_latest (transaction, block_a.hashables.account, hash, hash, block_a.hashables.balance, info.block_count + 1, true, epoch);
					if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
					{
						ledger.store.frontier_del (transaction, info.head);
					}
					// Frontier table is unnecessary for state blocks and this also prevents old blocks from being inserted on top of state blocks
					result.account = block_a.hashables.account;
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (rai::state_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == rai::process_result::progress)
	{
		result.code = validate_message (ledger.epoch_signer, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Unambiguous)
		if (result.code == rai::process_result::progress)
		{
			result.code = block_a.hashables.account.is_zero () ? rai::process_result::opened_burn_account : rai::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == rai::process_result::progress)
			{
				rai::account_info info;
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.code = block_a.hashables.previous.is_zero () ? rai::process_result::fork : rai::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == rai::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? rai::process_result::progress : rai::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == rai::process_result::progress)
						{
							result.code = block_a.hashables.previous == info.head ? rai::process_result::progress : rai::process_result::fork; // Is the previous block the account's head block? (Ambigious)
							if (result.code == rai::process_result::progress)
							{
								auto last_rep_block (ledger.store.block_get (transaction, info.rep_block));
								assert (last_rep_block != nullptr);
								result.code = block_a.hashables.representative == last_rep_block->representative () ? rai::process_result::progress : rai::process_result::representative_mismatch;
							}
						}
					}
				}
				else
				{
					result.code = block_a.hashables.representative.is_zero () ? rai::process_result::progress : rai::process_result::representative_mismatch;
				}
				if (result.code == rai::process_result::progress)
				{
					result.code = info.epoch == rai::epoch::epoch_0 ? rai::process_result::progress : rai::process_result::block_position;
					if (result.code == rai::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? rai::process_result::progress : rai::process_result::balance_mismatch;
						if (result.code == rai::process_result::progress)
						{
							ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::epoch_block);
							result.account = block_a.hashables.account;
							result.amount = 0;
							ledger.store.block_put (transaction, hash, block_a, 0, rai::epoch::epoch_1);
							ledger.change_latest (transaction, block_a.hashables.account, hash, hash, info.balance, info.block_count + 1, true, rai::epoch::epoch_1);
							if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
							{
								ledger.store.frontier_del (transaction, info.head);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (rai::change_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? rai::process_result::progress : rai::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? rai::process_result::progress : rai::process_result::block_position;
			if (result.code == rai::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? rai::process_result::fork : rai::process_result::progress;
				if (result.code == rai::process_result::progress)
				{
					rai::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					assert (!latest_error);
					assert (info.head == block_a.hashables.previous);
					result.code = validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
					if (result.code == rai::process_result::progress)
					{
						ledger.store.block_put (transaction, hash, block_a);
						auto balance (ledger.balance (transaction, block_a.hashables.previous));
						ledger.store.representation_add (transaction, hash, balance);
						ledger.store.representation_add (transaction, info.rep_block, 0 - balance);
						ledger.change_latest (transaction, account, hash, hash, info.balance, info.block_count + 1);
						ledger.store.frontier_del (transaction, block_a.hashables.previous);
						ledger.store.frontier_put (transaction, hash, account);
						result.account = account;
						result.amount = 0;
						ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::change);
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (rai::send_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? rai::process_result::progress : rai::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? rai::process_result::progress : rai::process_result::block_position;
			if (result.code == rai::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? rai::process_result::fork : rai::process_result::progress;
				if (result.code == rai::process_result::progress)
				{
					result.code = validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is this block signed correctly (Malformed)
					if (result.code == rai::process_result::progress)
					{
						rai::account_info info;
						auto latest_error (ledger.store.account_get (transaction, account, info));
						assert (!latest_error);
						assert (info.head == block_a.hashables.previous);
						result.code = info.balance.number () >= block_a.hashables.balance.number () ? rai::process_result::progress : rai::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
						if (result.code == rai::process_result::progress)
						{
							auto amount (info.balance.number () - block_a.hashables.balance.number ());
							ledger.store.representation_add (transaction, info.rep_block, 0 - amount);
							ledger.store.block_put (transaction, hash, block_a);
							ledger.change_latest (transaction, account, hash, info.rep_block, block_a.hashables.balance, info.block_count + 1);
							ledger.store.pending_put (transaction, rai::pending_key (block_a.hashables.destination, hash), { account, amount, rai::epoch::epoch_0 });
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.account = account;
							result.amount = amount;
							result.pending_account = block_a.hashables.destination;
							ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::send);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (rai::receive_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? rai::process_result::progress : rai::process_result::gap_previous;
		if (result.code == rai::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? rai::process_result::progress : rai::process_result::block_position;
			if (result.code == rai::process_result::progress)
			{
				result.code = (ledger.store.block_exists (transaction, rai::block_type::send, block_a.hashables.source) || ledger.store.block_exists (transaction, rai::block_type::state, block_a.hashables.source)) ? rai::process_result::progress : rai::process_result::gap_source; // Have we seen the source block already? (Harmless)
				if (result.code == rai::process_result::progress)
				{
					auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
					result.code = account.is_zero () ? rai::process_result::gap_previous : rai::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
					if (result.code == rai::process_result::progress)
					{
						result.code = rai::validate_message (account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
						if (result.code == rai::process_result::progress)
						{
							rai::account_info info;
							ledger.store.account_get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? rai::process_result::progress : rai::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == rai::process_result::progress)
							{
								rai::pending_key key (account, block_a.hashables.source);
								rai::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? rai::process_result::unreceivable : rai::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == rai::process_result::progress)
								{
									result.code = pending.epoch == rai::epoch::epoch_0 ? rai::process_result::progress : rai::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == rai::process_result::progress)
									{
										auto new_balance (info.balance.number () + pending.amount.number ());
										rai::account_info source_info;
										auto error (ledger.store.account_get (transaction, pending.source, source_info));
										assert (!error);
										ledger.store.pending_del (transaction, key);
										ledger.store.block_put (transaction, hash, block_a);
										ledger.change_latest (transaction, account, hash, info.rep_block, new_balance, info.block_count + 1);
										ledger.store.representation_add (transaction, info.rep_block, pending.amount.number ());
										ledger.store.frontier_del (transaction, block_a.hashables.previous);
										ledger.store.frontier_put (transaction, hash, account);
										result.account = account;
										result.amount = pending.amount;
										ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::receive);
									}
								}
							}
						}
					}
					else
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? rai::process_result::fork : rai::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
					}
				}
			}
		}
	}
}

void ledger_processor::open_block (rai::open_block const & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.store.block_exists (transaction, block_a.type (), hash));
	result.code = existing ? rai::process_result::old : rai::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == rai::process_result::progress)
	{
		auto source_missing (!ledger.store.block_exists (transaction, rai::block_type::send, block_a.hashables.source) && !ledger.store.block_exists (transaction, rai::block_type::state, block_a.hashables.source));
		result.code = source_missing ? rai::process_result::gap_source : rai::process_result::progress; // Have we seen the source block? (Harmless)
		if (result.code == rai::process_result::progress)
		{
			result.code = rai::validate_message (block_a.hashables.account, hash, block_a.signature) ? rai::process_result::bad_signature : rai::process_result::progress; // Is the signature valid (Malformed)
			if (result.code == rai::process_result::progress)
			{
				rai::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? rai::process_result::progress : rai::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == rai::process_result::progress)
				{
					rai::pending_key key (block_a.hashables.account, block_a.hashables.source);
					rai::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? rai::process_result::unreceivable : rai::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == rai::process_result::progress)
					{
						result.code = block_a.hashables.account == rai::burn_account ? rai::process_result::opened_burn_account : rai::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == rai::process_result::progress)
						{
							result.code = pending.epoch == rai::epoch::epoch_0 ? rai::process_result::progress : rai::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == rai::process_result::progress)
							{
								rai::account_info source_info;
								auto error (ledger.store.account_get (transaction, pending.source, source_info));
								assert (!error);
								ledger.store.pending_del (transaction, key);
								ledger.store.block_put (transaction, hash, block_a);
								ledger.change_latest (transaction, block_a.hashables.account, hash, hash, pending.amount.number (), info.block_count + 1);
								ledger.store.representation_add (transaction, hash, pending.amount.number ());
								ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
								result.account = block_a.hashables.account;
								result.amount = pending.amount;
								ledger.stats.inc (rai::stat::type::ledger, rai::stat::detail::open);
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (rai::ledger & ledger_a, rai::transaction const & transaction_a, bool valid_signature_a) :
ledger (ledger_a),
transaction (transaction_a),
valid_signature (valid_signature_a)
{
}
} // namespace

size_t rai::shared_ptr_block_hash::operator() (std::shared_ptr<rai::block> const & block_a) const
{
	auto hash (block_a->hash ());
	auto result (static_cast<size_t> (hash.qwords[0]));
	return result;
}

bool rai::shared_ptr_block_hash::operator() (std::shared_ptr<rai::block> const & lhs, std::shared_ptr<rai::block> const & rhs) const
{
	return lhs->hash () == rhs->hash ();
}

rai::ledger::ledger (rai::block_store & store_a, rai::stat & stat_a, rai::uint256_union const & epoch_link_a, rai::account const & epoch_signer_a) :
store (store_a),
stats (stat_a),
check_bootstrap_weights (true),
epoch_link (epoch_link_a),
epoch_signer (epoch_signer_a)
{
}

// Balance for account containing hash
rai::uint128_t rai::ledger::balance (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	rai::summation_visitor visitor (transaction_a, store);
	return visitor.compute_balance (hash_a);
}

// Balance for an account by account number
rai::uint128_t rai::ledger::account_balance (rai::transaction const & transaction_a, rai::account const & account_a)
{
	rai::uint128_t result (0);
	rai::account_info info;
	auto none (store.account_get (transaction_a, account_a, info));
	if (!none)
	{
		result = info.balance.number ();
	}
	return result;
}

rai::uint128_t rai::ledger::account_pending (rai::transaction const & transaction_a, rai::account const & account_a)
{
	rai::uint128_t result (0);
	rai::account end (account_a.number () + 1);
	for (auto i (store.pending_v0_begin (transaction_a, rai::pending_key (account_a, 0))), n (store.pending_v0_begin (transaction_a, rai::pending_key (end, 0))); i != n; ++i)
	{
		rai::pending_info info (i->second);
		result += info.amount.number ();
	}
	for (auto i (store.pending_v1_begin (transaction_a, rai::pending_key (account_a, 0))), n (store.pending_v1_begin (transaction_a, rai::pending_key (end, 0))); i != n; ++i)
	{
		rai::pending_info info (i->second);
		result += info.amount.number ();
	}
	return result;
}

rai::process_return rai::ledger::process (rai::transaction const & transaction_a, rai::block const & block_a, bool valid_signature)
{
	ledger_processor processor (*this, transaction_a, valid_signature);
	block_a.visit (processor);
	return processor.result;
}

rai::block_hash rai::ledger::representative (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

rai::block_hash rai::ledger::representative_calculated (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool rai::ledger::block_exists (rai::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, hash_a));
	return result;
}

bool rai::ledger::block_exists (rai::block_type type, rai::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto result (store.block_exists (transaction, type, hash_a));
	return result;
}

std::string rai::ledger::block_text (char const * hash_a)
{
	return block_text (rai::block_hash (hash_a));
}

std::string rai::ledger::block_text (rai::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool rai::ledger::is_send (rai::transaction const & transaction_a, rai::state_block const & block_a)
{
	bool result (false);
	rai::block_hash previous (block_a.hashables.previous);
	if (!previous.is_zero ())
	{
		if (block_a.hashables.balance < balance (transaction_a, previous))
		{
			result = true;
		}
	}
	return result;
}

rai::block_hash rai::ledger::block_destination (rai::transaction const & transaction_a, rai::block const & block_a)
{
	rai::block_hash result (0);
	rai::send_block const * send_block (dynamic_cast<rai::send_block const *> (&block_a));
	rai::state_block const * state_block (dynamic_cast<rai::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		result = send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

rai::block_hash rai::ledger::block_source (rai::transaction const & transaction_a, rai::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	assert (block_a.previous ().is_zero () || store.block_exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	rai::block_hash result (block_a.source ());
	rai::state_block const * state_block (dynamic_cast<rai::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link;
	}
	return result;
}

// Vote weight of an account
rai::uint128_t rai::ledger::weight (rai::transaction const & transaction_a, rai::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		auto blocks = store.block_count (transaction_a);
		if (blocks.sum () < bootstrap_weight_max_blocks)
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
	return store.representation_get (transaction_a, account_a);
}

// Rollback blocks until `block_a' doesn't exist
void rai::ledger::rollback (rai::transaction const & transaction_a, rai::block_hash const & block_a)
{
	assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this);
	rai::account_info info;
	while (store.block_exists (transaction_a, block_a))
	{
		auto latest_error (store.account_get (transaction_a, account_l, info));
		assert (!latest_error);
		auto block (store.block_get (transaction_a, info.head));
		block->visit (rollback);
	}
}

// Return account containing hash
rai::account rai::ledger::account (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	rai::account result;
	auto hash (hash_a);
	rai::block_hash successor (1);
	rai::block_info block_info;
	auto block (store.block_get (transaction_a, hash));
	assert (block);
	while (!successor.is_zero () && block->type () != rai::block_type::state && store.block_info_get (transaction_a, successor, block_info))
	{
		successor = store.block_successor (transaction_a, hash);
		if (!successor.is_zero ())
		{
			hash = successor;
			block = store.block_get (transaction_a, hash);
		}
	}
	if (block->type () == rai::block_type::state)
	{
		auto state_block (dynamic_cast<rai::state_block *> (block.get ()));
		result = state_block->hashables.account;
	}
	else if (successor.is_zero ())
	{
		result = store.frontier_get (transaction_a, hash);
	}
	else
	{
		result = block_info.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return amount decrease or increase for block
rai::uint128_t rai::ledger::amount (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	summation_visitor amount (transaction_a, store);
	return amount.compute_amount (hash_a);
}

// Return latest block for account
rai::block_hash rai::ledger::latest (rai::transaction const & transaction_a, rai::account const & account_a)
{
	rai::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number of there are no blocks for this account.
rai::block_hash rai::ledger::latest_root (rai::transaction const & transaction_a, rai::account const & account_a)
{
	rai::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	rai::block_hash result;
	if (latest_error)
	{
		result = account_a;
	}
	else
	{
		result = info.head;
	}
	return result;
}

rai::checksum rai::ledger::checksum (rai::transaction const & transaction_a, rai::account const & begin_a, rai::account const & end_a)
{
	rai::checksum result;
	auto error (store.checksum_get (transaction_a, 0, 0, result));
	assert (!error);
	return result;
}

void rai::ledger::dump_account_chain (rai::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		assert (block != nullptr);
		std::cerr << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

class block_fit_visitor : public rai::block_visitor
{
public:
	block_fit_visitor (rai::ledger & ledger_a, rai::transaction const & transaction_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	result (false)
	{
	}
	void send_block (rai::send_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void receive_block (rai::receive_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
		result &= ledger.store.block_exists (transaction, block_a.source ());
	}
	void open_block (rai::open_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.source ());
	}
	void change_block (rai::change_block const & block_a) override
	{
		result = ledger.store.block_exists (transaction, block_a.previous ());
	}
	void state_block (rai::state_block const & block_a) override
	{
		result = block_a.previous ().is_zero () || ledger.store.block_exists (transaction, block_a.previous ());
		if (result && !ledger.is_send (transaction, block_a))
		{
			result &= ledger.store.block_exists (transaction, block_a.hashables.link) || block_a.hashables.link.is_zero () || ledger.is_epoch_link (block_a.hashables.link);
		}
	}
	rai::ledger & ledger;
	rai::transaction const & transaction;
	bool result;
};

bool rai::ledger::could_fit (rai::transaction const & transaction_a, rai::block const & block_a)
{
	block_fit_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

bool rai::ledger::is_epoch_link (rai::uint256_union const & link_a)
{
	return link_a == epoch_link;
}

void rai::ledger::checksum_update (rai::transaction const & transaction_a, rai::block_hash const & hash_a)
{
	rai::checksum value;
	auto error (store.checksum_get (transaction_a, 0, 0, value));
	assert (!error);
	value ^= hash_a;
	store.checksum_put (transaction_a, 0, 0, value);
}

void rai::ledger::change_latest (rai::transaction const & transaction_a, rai::account const & account_a, rai::block_hash const & hash_a, rai::block_hash const & rep_block_a, rai::amount const & balance_a, uint64_t block_count_a, bool is_state, rai::epoch epoch_a)
{
	rai::account_info info;
	auto exists (!store.account_get (transaction_a, account_a, info));
	if (exists)
	{
		checksum_update (transaction_a, info.head);
	}
	else
	{
		assert (store.block_get (transaction_a, hash_a)->previous ().is_zero ());
		info.open_block = hash_a;
	}
	if (!hash_a.is_zero ())
	{
		info.head = hash_a;
		info.rep_block = rep_block_a;
		info.balance = balance_a;
		info.modified = rai::seconds_since_epoch ();
		info.block_count = block_count_a;
		if (exists && info.epoch != epoch_a)
		{
			// otherwise we'd end up with a duplicate
			store.account_del (transaction_a, account_a);
		}
		info.epoch = epoch_a;
		store.account_put (transaction_a, account_a, info);
		if (!(block_count_a % store.block_info_max) && !is_state)
		{
			rai::block_info block_info;
			block_info.account = account_a;
			block_info.balance = balance_a;
			store.block_info_put (transaction_a, hash_a, block_info);
		}
		checksum_update (transaction_a, hash_a);
	}
	else
	{
		store.account_del (transaction_a, account_a);
	}
}

std::shared_ptr<rai::block> rai::ledger::successor (rai::transaction const & transaction_a, rai::uint256_union const & root_a)
{
	rai::block_hash successor (0);
	if (store.account_exists (transaction_a, root_a))
	{
		rai::account_info info;
		auto error (store.account_get (transaction_a, root_a, info));
		assert (!error);
		successor = info.open_block;
	}
	else
	{
		successor = store.block_successor (transaction_a, root_a);
	}
	std::shared_ptr<rai::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<rai::block> rai::ledger::forked_block (rai::transaction const & transaction_a, rai::block const & block_a)
{
	assert (!store.block_exists (transaction_a, block_a.type (), block_a.hash ()));
	auto root (block_a.root ());
	assert (store.block_exists (transaction_a, root) || store.account_exists (transaction_a, root));
	auto result (store.block_get (transaction_a, store.block_successor (transaction_a, root)));
	if (result == nullptr)
	{
		rai::account_info info;
		auto error (store.account_get (transaction_a, root, info));
		assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		assert (result != nullptr);
	}
	return result;
}
