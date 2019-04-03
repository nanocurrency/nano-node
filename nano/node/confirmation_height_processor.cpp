#include <boost/optional.hpp>
#include <boost/polymorphic_pointer_cast.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/node.hpp> // For active_transactions
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

nano::confirmation_height_processor::confirmation_height_processor (nano::block_store & store, nano::ledger & ledger, nano::active_transactions & active) :
store (store), ledger (ledger), active (active), thread ([this]() {
	nano::thread_role::set (nano::thread_role::name::confirmation_height_processing);
	this->run ();
})
{
}

nano::confirmation_height_processor::~confirmation_height_processor ()
{
	stop ();
}

void nano::confirmation_height_processor::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::confirmation_height_processor::run ()
{
	std::unique_lock<std::mutex> lk (mutex);
	while (!stopped)
	{
		if (!pending_confirmations.empty ())
		{
			auto pending_confirmation = pending_confirmations.front ();
			pending_confirmations.pop ();
			lk.unlock ();
			add_confirmation_height (pending_confirmation);
			lk.lock ();
		}
		else
		{
			condition.wait (lk);
		}
	}
}

void nano::confirmation_height_processor::add (nano::block_hash const & hash_a)
{
	{
		std::lock_guard<std::mutex> lk (mutex);
		pending_confirmations.push (hash_a);
	}
	condition.notify_one ();
}

/**
 * For all the blocks below this height which have been implicitly confirmed check if they
 * are open/receive blocks, and if so follow the source blocks and iteratively repeat to genesis.
 * To limit write locking and to keep the confirmation height ledger correctly synced, confirmations are
 * written from the ground upwards in batches.
 */
void nano::confirmation_height_processor::add_confirmation_height (nano::block_hash const & hash_a)
{
	std::stack<open_receive_source_pair> open_receive_source_pairs;
	auto current = hash_a;
	boost::optional<open_receive_details> open_receive_details;
	nano::account_info account_info;
	nano::genesis genesis;
	auto genesis_hash = genesis.hash ();
	std::unordered_map<nano::account, block_hash_height_pair> pending;

	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();

	do
	{
		if (!open_receive_source_pairs.empty ())
		{
			open_receive_details = open_receive_source_pairs.top ().open_receive_details;
			current = open_receive_source_pairs.top ().source_hash;
		}

		auto transaction (store.tx_begin_read ());
		auto block_height = (store.block_account_height (transaction, current));
		nano::account account (ledger.account (transaction, current));
		release_assert (!store.account_get (transaction, account, account_info));
		auto confirmation_height = account_info.confirmation_height;
		auto count_before_open_receive = open_receive_source_pairs.size ();

		auto hash (current);
		if (block_height > confirmation_height)
		{
			collect_unconfirmed_receive_and_sources_for_account (block_height, confirmation_height, current, genesis_hash, open_receive_source_pairs, account, transaction);
		}

		// If this adds no more open_receive blocks, then we can now confirm this account as well as the linked open/receive block
		// Collect as pending any writes to the database and do them in bulk after a certain time.
		auto confirmed_receives_pending = (count_before_open_receive != open_receive_source_pairs.size ());
		if (!confirmed_receives_pending)
		{
			if (block_height > confirmation_height)
			{
				update_confirmation_height (account, { hash, block_height }, pending);
			}

			if (open_receive_details)
			{
				update_confirmation_height (open_receive_details->account, open_receive_details->block_hash_height_pair, pending);
			}

			if (!open_receive_source_pairs.empty ())
			{
				open_receive_source_pairs.pop ();
			}
		}

		// Check whether writing to the database should be done now
		if ((timer.after_deadline (batch_write_delta) || open_receive_source_pairs.empty ()) && !pending.empty ())
		{
			write_pending (pending);
			timer.restart ();
		}
	} while (!open_receive_source_pairs.empty ());
}

void nano::confirmation_height_processor::update_confirmation_height (nano::account const & account_a, block_hash_height_pair const & block_hash_height_pair_a, std::unordered_map<nano::account, block_hash_height_pair> & pending)
{
	auto it = pending.find (account_a);
	if (it != pending.end ())
	{
		if (block_hash_height_pair_a.height > it->second.height)
		{
			it->second = block_hash_height_pair_a;
		}
	}
	else
	{
		pending.emplace (account_a, block_hash_height_pair_a);
	}
}

void nano::confirmation_height_processor::write_pending (std::unordered_map<nano::account, block_hash_height_pair> & all_pending)
{
	nano::account_info account_info;
	auto transaction (store.tx_begin_write ());
	for (auto & pending : all_pending)
	{
		auto error = store.account_get (transaction, pending.first, account_info);
		release_assert (!error);
		if (pending.second.height > account_info.confirmation_height)
		{
			// Check that the confirmation height and block hash still match, as there
			// may have been changes outside this processor
			nano::block_sideband sideband;
			auto block = store.block_get (transaction, pending.second.hash, &sideband);
			release_assert (block != nullptr);
			release_assert (sideband.height == pending.second.height);

			account_info.confirmation_height = pending.second.height;
			store.account_put (transaction, pending.first, account_info);
		}
	}

	all_pending.clear ();
}

void nano::confirmation_height_processor::collect_unconfirmed_receive_and_sources_for_account (uint64_t block_height, uint64_t confirmation_height, nano::block_hash & current, const nano::block_hash & genesis_hash, std::stack<open_receive_source_pair> & open_receive_source_pairs, nano::account const & account, nano::transaction & transaction)
{
	// Get the last confirmed block in this account chain
	auto num_to_confirm = block_height - confirmation_height;
	while (num_to_confirm > 0 && !current.is_zero ())
	{
		active.confirm_block (current);
		auto block (store.block_get (transaction, current));
		if (block)
		{
			if (block->type () == nano::block_type::receive || (block->type () == nano::block_type::open && block->hash () != genesis_hash))
			{
				open_receive_source_pairs.emplace (account, block_hash_height_pair{ current, confirmation_height + num_to_confirm }, block->source ());
			}
			else
			{
				// Then check state blocks
				auto state = boost::dynamic_pointer_cast<nano::state_block> (block);
				if (state != nullptr)
				{
					nano::block_hash previous (state->hashables.previous);
					if (!previous.is_zero ())
					{
						if (state->hashables.balance.number () >= ledger.balance (transaction, previous) && !state->hashables.link.is_zero () && !ledger.is_epoch_link (state->hashables.link))
						{
							open_receive_source_pairs.emplace (account, block_hash_height_pair{ current, confirmation_height + num_to_confirm }, state->hashables.link);
						}
					}
					// State open blocks are always receive or epoch
					else if (!ledger.is_epoch_link (state->hashables.link))
					{
						open_receive_source_pairs.emplace (account, block_hash_height_pair{ current, confirmation_height + num_to_confirm }, state->hashables.link);
					}
				}
			}
			current = block->previous ();
		}
		--num_to_confirm;
	}
}

namespace nano
{
confirmation_height_processor::block_hash_height_pair::block_hash_height_pair (block_hash const & hash_a, uint64_t height_a) :
hash (hash_a), height (height_a)
{
}

confirmation_height_processor::open_receive_details::open_receive_details (nano::account const & account_a, const confirmation_height_processor::block_hash_height_pair & block_hash_height_pair_a) :
account (account_a),
block_hash_height_pair (block_hash_height_pair_a)
{
}

confirmation_height_processor::open_receive_source_pair::open_receive_source_pair (account const & account_a, confirmation_height_processor::block_hash_height_pair const & block_hash_height_pair_a,
const block_hash & source_a) :
open_receive_details (account_a, block_hash_height_pair_a),
source_hash (source_a)
{
}
}
