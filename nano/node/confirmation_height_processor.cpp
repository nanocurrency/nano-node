#include <nano/node/confirmation_height_processor.hpp>

#include <boost/optional.hpp>
#include <cassert>
#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/active_transactions.hpp>
#include <nano/node/stats.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <numeric>

nano::confirmation_height_processor::confirmation_height_processor (nano::pending_confirmation_height & pending_confirmation_height, nano::block_store & store, nano::ledger & ledger, nano::active_transactions & active, nano::logger_mt & logger) :
pending_confirmations (pending_confirmation_height),
store (store),
ledger (ledger),
active (active),
logger (logger),
thread ([this]() {
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
	stopped = true;
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void nano::confirmation_height_processor::run ()
{
	std::unique_lock<std::mutex> lk (pending_confirmations.mutex);
	while (!stopped)
	{
		if (!pending_confirmations.pending.empty ())
		{
			pending_confirmations.current_hash = *pending_confirmations.pending.begin ();
			pending_confirmations.pending.erase (pending_confirmations.current_hash);
			// Copy the hash so can be used outside owning the lock
			auto current_pending_block = pending_confirmations.current_hash;
			lk.unlock ();
			add_confirmation_height (current_pending_block);
			lk.lock ();
			pending_confirmations.current_hash = 0;
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
		std::lock_guard<std::mutex> lk (pending_confirmations.mutex);
		pending_confirmations.pending.insert (hash_a);
	}
	condition.notify_one ();
}

// This only check top-level blocks having their confirmation height sets, not anything below
bool nano::confirmation_height_processor::is_processing_block (nano::block_hash const & hash_a)
{
	return pending_confirmations.is_processing_block (hash_a);
}

/**
 * For all the blocks below this height which have been implicitly confirmed check if they
 * are open/receive blocks, and if so follow the source blocks and iteratively repeat to genesis.
 * To limit write locking and to keep the confirmation height ledger correctly synced, confirmations are
 * written from the ground upwards in batches.
 */
void nano::confirmation_height_processor::add_confirmation_height (nano::block_hash const & hash_a)
{
	boost::optional<conf_height_details> receive_details;
	auto current = hash_a;
	nano::account_info account_info;
	std::deque<conf_height_details> pending_writes;
	release_assert (receive_source_pairs_size == 0);

	// Store the highest confirmation heights for accounts in pending_writes to reduce unnecessary iterating
	std::unordered_map<account, uint64_t> confirmation_height_pending_write_cache;

	release_assert (receive_source_pairs.empty ());
	auto error = false;
	// Traverse account chain and all sources for receive blocks iteratively
	do
	{
		if (!receive_source_pairs.empty ())
		{
			receive_details = receive_source_pairs.back ().receive_details;
			current = receive_source_pairs.back ().source_hash;
		}
		auto transaction (store.tx_begin_read ());
		auto block_height (store.block_account_height (transaction, current));
		nano::account account (ledger.account (transaction, current));
		release_assert (!store.account_get (transaction, account, account_info));
		auto confirmation_height = account_info.confirmation_height;

		auto account_it = confirmation_height_pending_write_cache.find (account);
		if (account_it != confirmation_height_pending_write_cache.cend () && account_it->second > confirmation_height)
		{
			confirmation_height = account_it->second;
		}

		auto count_before_open_receive = receive_source_pairs.size ();
		if (block_height > confirmation_height)
		{
			if ((block_height - confirmation_height) > 20000)
			{
				logger.always_log ("Iterating over a large account chain for setting confirmation height. The top block: ", current.to_string ());
			}

			collect_unconfirmed_receive_and_sources_for_account (block_height, confirmation_height, current, account, transaction);
		}

		// If this adds no more open_receive blocks, then we can now confirm this account as well as the linked open/receive block
		// Collect as pending any writes to the database and do them in bulk after a certain time.
		auto confirmed_receives_pending = (count_before_open_receive != receive_source_pairs.size ());
		if (!confirmed_receives_pending)
		{
			if (block_height > confirmation_height)
			{
				// Check whether the previous block has been seen. If so, the rest of sends below have already been seen so don't count them
				if (account_it != confirmation_height_pending_write_cache.cend ())
				{
					account_it->second = block_height;
				}
				else
				{
					confirmation_height_pending_write_cache.emplace (account, block_height);
				}

				pending_writes.emplace_back (account, current, block_height, block_height - confirmation_height);
			}

			if (receive_details)
			{
				// Check whether the previous block has been seen. If so, the rest of sends below have already been seen so don't count them
				auto const & receive_account = receive_details->account;
				auto receive_account_it = confirmation_height_pending_write_cache.find (receive_account);
				if (receive_account_it != confirmation_height_pending_write_cache.cend ())
				{
					// Get current height
					auto current_height = receive_account_it->second;
					receive_account_it->second = receive_details->height;
					receive_details->num_blocks_confirmed = receive_details->height - current_height;
				}
				else
				{
					confirmation_height_pending_write_cache.emplace (receive_account, receive_details->height);
				}

				pending_writes.push_back (*receive_details);
			}

			if (!receive_source_pairs.empty ())
			{
				// Pop from the end
				receive_source_pairs.erase (receive_source_pairs.end () - 1);
				--receive_source_pairs_size;
			}
		}

		// Check whether writing to the database should be done now
		auto total_pending_write_block_count = std::accumulate (pending_writes.cbegin (), pending_writes.cend (), uint64_t (0), [](uint64_t total, conf_height_details const & conf_height_details_a) {
			return total += conf_height_details_a.num_blocks_confirmed;
		});

		if ((total_pending_write_block_count >= batch_write_size || receive_source_pairs.empty ()) && !pending_writes.empty ())
		{
			error = write_pending (pending_writes, total_pending_write_block_count);
			// Don't set any more blocks as confirmed from the original hash if an inconsistency is found
			if (error)
			{
				receive_source_pairs.clear ();
				receive_source_pairs_size = 0;
				break;
			}
			assert (pending_writes.empty ());
		}
		// Exit early when the processor has been stopped, otherwise this function may take a
		// while (and hence keep the process running) if updating a long chain.
		if (stopped)
		{
			break;
		}
	} while (!receive_source_pairs.empty ());

	// Now confirm the block that was originally passed in.
	if (!error)
	{
		assert (pending_writes.empty ());
		write_remaining_unconfirmed_non_receive_blocks (store, hash_a);
	}
}

/*
 * This takes a block and confirms the blocks below it. It assumes that all other blocks below the first receive has already been confirmed.
 */
void nano::confirmation_height_processor::write_remaining_unconfirmed_non_receive_blocks (nano::block_store & store, nano::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	auto block_height (store.block_account_height (transaction, hash_a));
	nano::account_info account_info;
	nano::account account (ledger.account (transaction, hash_a));
	release_assert (!store.account_get (transaction, account, account_info));
	auto confirmation_height = account_info.confirmation_height;

	if (block_height > confirmation_height)
	{
		auto count_before_open_receive = receive_source_pairs.size ();
		collect_unconfirmed_receive_and_sources_for_account (block_height, confirmation_height, hash_a, account, transaction);
		auto confirmed_receives_pending = (count_before_open_receive != receive_source_pairs.size ());

		// There should be no receive blocks left requiring confirmation for this account chain
		release_assert (!confirmed_receives_pending);
	}
	auto num_blocks_confirmed = block_height - confirmation_height;
	std::deque<conf_height_details> pending_writes;
	pending_writes.emplace_back (account, hash_a, block_height, num_blocks_confirmed);

	if (!pending_writes.empty ())
	{
		assert (pending_writes.size () == 1);
		auto error = write_pending (pending_writes, num_blocks_confirmed);
		assert (!error);
	}
}

/*
 * Returns true if there was an error in finding one of the blocks to write a confirmation height for, false otherwise
 */
bool nano::confirmation_height_processor::write_pending (std::deque<conf_height_details> & all_pending, int64_t total_pending_write_block_count_a)
{
	nano::account_info account_info;
	auto total_pending_write_block_count (total_pending_write_block_count_a);

	// Write in batches
	while (total_pending_write_block_count > 0)
	{
		uint64_t num_block_writes = 0;
		auto transaction (store.tx_begin_write ());
		while (!all_pending.empty ())
		{
			const auto & pending = all_pending.front ();
			auto error = store.account_get (transaction, pending.account, account_info);
			release_assert (!error);
			if (pending.height > account_info.confirmation_height)
			{
#ifndef NDEBUG
				// Do more thorough checking in Debug mode, indicates programming error.
				nano::block_sideband sideband;
				auto block = store.block_get (transaction, pending.hash, &sideband);
				assert (block != nullptr);
				assert (sideband.height == pending.height);
#else
				auto block = store.block_get (transaction, pending.hash);
#endif
				// Check that the block still exists as there may have been changes outside this processor.
				if (!block)
				{
					logger.always_log ("Failed to write confirmation height for: ", pending.hash.to_string ());
					ledger.stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::invalid_block);
					return true;
				}

				ledger.stats.add (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in, pending.height - account_info.confirmation_height);
				assert (pending.num_blocks_confirmed == pending.height - account_info.confirmation_height);
				account_info.confirmation_height = pending.height;
				store.account_put (transaction, pending.account, account_info);
			}
			total_pending_write_block_count -= pending.num_blocks_confirmed;
			num_block_writes += pending.num_blocks_confirmed;
			all_pending.erase (all_pending.begin ());

			if (num_block_writes >= batch_write_size)
			{
				// Commit changes periodically to reduce time holding write locks for long chains
				break;
			}
		}
	}
	return false;
}

/*
 * This function assumes receive_source_pairs_lk is not already locked
 */
void nano::confirmation_height_processor::collect_unconfirmed_receive_and_sources_for_account (uint64_t block_height, uint64_t confirmation_height, nano::block_hash const & current, nano::account const & account, nano::transaction & transaction)
{
	auto hash (current);
	auto num_to_confirm = block_height - confirmation_height;

	// Store heights of blocks
	constexpr auto height_not_set = std::numeric_limits<uint64_t>::max ();
	auto next_height = height_not_set;
	while (num_to_confirm > 0 && !hash.is_zero ())
	{
		active.confirm_block (hash);
		nano::block_sideband sideband;
		auto block (store.block_get (transaction, hash, &sideband));
		if (block)
		{
			auto source (block->source ());
			if (source.is_zero ())
			{
				source = block->link ();
			}

			if (store.source_exists (transaction, source))
			{
				// Set the height for the receive block above (if there is one)
				if (next_height != height_not_set)
				{
					receive_source_pairs.back ().receive_details.num_blocks_confirmed = next_height - sideband.height;
				}

				receive_source_pairs.emplace_back (conf_height_details{ account, hash, sideband.height, height_not_set }, source);
				++receive_source_pairs_size;
				next_height = sideband.height;
			}

			hash = block->previous ();
		}
		--num_to_confirm;
	}

	// Update the number of blocks confirmed by the last receive block
	if (!receive_source_pairs.empty ())
	{
		receive_source_pairs.back ().receive_details.num_blocks_confirmed = receive_source_pairs.back ().receive_details.height - confirmation_height;
	}
}

namespace nano
{
confirmation_height_processor::conf_height_details::conf_height_details (nano::account const & account_a, nano::block_hash const & hash_a, uint64_t height_a, uint64_t num_blocks_confirmed_a) :
account (account_a),
hash (hash_a),
height (height_a),
num_blocks_confirmed (num_blocks_confirmed_a)
{
}

confirmation_height_processor::receive_source_pair::receive_source_pair (confirmation_height_processor::conf_height_details const & receive_details_a, const block_hash & source_a) :
receive_details (receive_details_a),
source_hash (source_a)
{
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor & confirmation_height_processor, const std::string & name)
{
	size_t receive_source_pairs_count = confirmation_height_processor.receive_source_pairs_size;
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "receive_source_pairs", receive_source_pairs_count, sizeof (decltype (confirmation_height_processor.receive_source_pairs)::value_type) }));
	return composite;
}
}

size_t nano::pending_confirmation_height::size ()
{
	std::lock_guard<std::mutex> lk (mutex);
	return pending.size ();
}

bool nano::pending_confirmation_height::is_processing_block (nano::block_hash const & hash_a)
{
	// First check the hash currently being processed
	std::lock_guard<std::mutex> lk (mutex);
	if (!current_hash.is_zero () && current_hash == hash_a)
	{
		return true;
	}

	// Check remaining pending confirmations
	return pending.find (hash_a) != pending.cend ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height & pending_confirmation_height, const std::string & name)
{
	size_t pending_count = pending_confirmation_height.size ();
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pending", pending_count, sizeof (nano::block_hash) }));
	return composite;
}
}
