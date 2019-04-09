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

constexpr std::chrono::milliseconds nano::confirmation_height_processor::batch_write_delta;

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
		if (!pending_confirmations.queue.empty ())
		{
			auto pending_confirmation = pending_confirmations.queue.front ();
			pending_confirmations.queue.pop ();
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
		std::lock_guard<std::mutex> lk (pending_confirmations.mutex);
		pending_confirmations.queue.push (hash_a);
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
	boost::optional<conf_height_details> receive_details;
	auto current = hash_a;
	nano::account_info account_info;
	nano::genesis genesis;
	auto genesis_hash = genesis.hash ();
	std::queue<conf_height_details> pending;

	nano::timer<std::chrono::milliseconds> timer;
	timer.start ();

	std::unique_lock <std::mutex> receive_source_pairs_lk (receive_source_pairs_mutex);
	release_assert (receive_source_pairs.empty ());
	do
	{
		if (!receive_source_pairs.empty ())
		{
			receive_details = receive_source_pairs.top ().receive_details;
			current = receive_source_pairs.top ().source_hash;
		}
		receive_source_pairs_lk.unlock ();
		auto transaction (store.tx_begin_read ());
		auto block_height = (store.block_account_height (transaction, current));
		nano::account account (ledger.account (transaction, current));
		release_assert (!store.account_get (transaction, account, account_info));
		auto confirmation_height = account_info.confirmation_height;
		receive_source_pairs_lk.lock ();
		auto count_before_open_receive = receive_source_pairs.size ();

		auto hash (current);
		if (block_height > confirmation_height)
		{
			collect_unconfirmed_receive_and_sources_for_account (block_height, confirmation_height, current, genesis_hash, receive_source_pairs, account, transaction);
		}

		// If this adds no more open_receive blocks, then we can now confirm this account as well as the linked open/receive block
		// Collect as pending any writes to the database and do them in bulk after a certain time.
		auto confirmed_receives_pending = (count_before_open_receive != receive_source_pairs.size ());
		receive_source_pairs_lk.unlock ();
		if (!confirmed_receives_pending)
		{
			if (block_height > confirmation_height)
			{
				pending.emplace (account, hash, block_height);
			}

			if (receive_details)
			{
				pending.push (*receive_details);
			}

			receive_source_pairs_lk.lock ();
			if (!receive_source_pairs.empty ())
			{
				receive_source_pairs.pop ();
			}
			receive_source_pairs_lk.unlock ();
		}
		// Check whether writing to the database should be done now
		receive_source_pairs_lk.lock ();
		if ((timer.after_deadline (batch_write_delta) || receive_source_pairs.empty ()) && !pending.empty ())
		{
			auto error = write_pending (pending);

			// Don't set any more blocks as confirmed from the original hash if an inconsistency is found
			if (error)
			{
				break;
			}
			assert (pending.empty ());
			timer.restart ();
		}
		receive_source_pairs_lk.unlock ();
		// Exit early when the processor has been stopped, otherwise this function may take a
		// while (and hence keep the process running) if updating a long chain.
		if (stopped)
		{
			break;
		}
		receive_source_pairs_lk.lock ();
	} while (!receive_source_pairs.empty ());
}

// Returns true if there was an error in finding one of the blocks to write the confirmation height for.
bool nano::confirmation_height_processor::write_pending (std::queue<conf_height_details> & all_pending)
{
	nano::account_info account_info;
	auto transaction (store.tx_begin_write ());
	while (!all_pending.empty ())
	{
		const auto & pending = all_pending.front ();
		auto error = store.account_get (transaction, pending.account, account_info);
		release_assert (!error);
		if (pending.height > account_info.confirmation_height)
		{
#ifdef NDEBUG
			auto block = store.block_get (transaction, pending.hash);
#else
			// Do more thorough checking in Debug mode
			nano::block_sideband sideband;
			auto block = store.block_get (transaction, pending.hash, &sideband);
			assert (block != nullptr);
			assert (sideband.height == pending.height);
#endif
			// Check that the block still exists as there may have been changes outside this processor.
			if (!block)
			{
				logger.always_log ("Failed to write confirmation height for: ", pending.hash.to_string ());
				ledger.stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::invalid_block);
				return true;
			}

			ledger.stats.add (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in, pending.height - account_info.confirmation_height);
			account_info.confirmation_height = pending.height;
			store.account_put (transaction, pending.account, account_info);
		}
		all_pending.pop ();
	}
	return false;
}

void nano::confirmation_height_processor::collect_unconfirmed_receive_and_sources_for_account (uint64_t block_height, uint64_t confirmation_height, nano::block_hash & current, const nano::block_hash & genesis_hash, std::stack<receive_source_pair> & receive_source_pairs, nano::account const & account, nano::transaction & transaction)
{
	// Get the last confirmed block in this account chain
	auto num_to_confirm = block_height - confirmation_height;
	while (num_to_confirm > 0 && !current.is_zero ())
	{
		active.confirm_block (current);
		nano::block_sideband sideband;
		auto block (store.block_get (transaction, current, &sideband));
		if (block)
		{
			if (block->type () == nano::block_type::receive || (block->type () == nano::block_type::open && block->hash () != genesis_hash))
			{
				receive_source_pairs.emplace (conf_height_details{ account, current, sideband.height }, block->source ());
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
							receive_source_pairs.emplace (conf_height_details{ account, current, sideband.height }, state->hashables.link);
						}
					}
					// State open blocks are always receive or epoch
					else if (!ledger.is_epoch_link (state->hashables.link))
					{
						receive_source_pairs.emplace (conf_height_details{ account, current, sideband.height }, state->hashables.link);
					}
				}
			}
			current = block->previous ();
		}
		--num_to_confirm;
	}
}

size_t nano::confirmation_height_processor::receive_source_pairs_size ()
{
	std::lock_guard<std::mutex> lk (receive_source_pairs_mutex);
	return receive_source_pairs.size ();
}

namespace nano
{
confirmation_height_processor::conf_height_details::conf_height_details (nano::account const & account_a, nano::block_hash const & hash_a, uint64_t height_a) :
account (account_a),
hash (hash_a),
height (height_a)
{
}

confirmation_height_processor::receive_source_pair::receive_source_pair (confirmation_height_processor::conf_height_details const & receive_details_a, const block_hash & source_a) :
receive_details (receive_details_a),
source_hash (source_a)
{
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor & confirmation_height_processor, const std::string & name)
{
	size_t receive_source_pairs_count = confirmation_height_processor.receive_source_pairs_size ();
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "receive_source_pairs", receive_source_pairs_count, sizeof (decltype (confirmation_height_processor.receive_source_pairs)::value_type) }));
	return composite;
}
}

size_t nano::pending_confirmation_height::size ()
{
	std::lock_guard<std::mutex> lk (mutex);
	return queue.size ();
}

namespace nano
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height & pending_confirmation_height, const std::string & name)
{
	size_t queue_count = pending_confirmation_height.size ();
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "queue", queue_count, sizeof (nano::block_hash) }));
	return composite;
}
}