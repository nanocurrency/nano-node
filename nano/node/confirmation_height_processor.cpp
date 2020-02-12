#include <nano/lib/logger_mt.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/confirmation_height_processor.hpp>
#include <nano/node/write_database_queue.hpp>
#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <boost/optional.hpp>

#include <cassert>
#include <numeric>

nano::confirmation_height_processor::confirmation_height_processor (nano::ledger & ledger_a, nano::write_database_queue & write_database_queue_a, std::chrono::milliseconds batch_separate_pending_min_time_a, nano::logger_mt & logger_a) :
ledger (ledger_a),
logger (logger_a),
write_database_queue (write_database_queue_a),
batch_separate_pending_min_time (batch_separate_pending_min_time_a),
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
	nano::unique_lock<std::mutex> lk (mutex);
	while (!stopped)
	{
		if (!paused && !awaiting_processing.empty ())
		{
			lk.unlock ();
			if (pending_writes.empty ())
			{
				// Separate blocks which are pending confirmation height can be batched by a minimum processing time (to improve lmdb disk write performance),
				// so make sure the slate is clean when a new batch is starting.
				accounts_confirmed_info.clear ();
				accounts_confirmed_info_size = 0;
				timer.restart ();
			}
			set_next_hash ();
			process ();
			lk.lock ();
		}
		else
		{
			// If there are blocks pending cementing, then make sure we flush out the remaining writes
			lk.unlock ();
			if (!pending_writes.empty ())
			{
				auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
				cement_blocks ();
				lk.lock ();
				original_hash.clear ();
			}
			else
			{
				lk.lock ();
				original_hash.clear ();
				condition.wait (lk);
			}
		}
	}
}

// Pausing only affects processing new blocks, not the current one being processed. Currently only used in tests
void nano::confirmation_height_processor::pause ()
{
	paused = true;
}

void nano::confirmation_height_processor::unpause ()
{
	paused = false;
	condition.notify_one ();
}

void nano::confirmation_height_processor::add (nano::block_hash const & hash_a)
{
	{
		nano::lock_guard<std::mutex> lk (mutex);
		awaiting_processing.insert (hash_a);
	}
	condition.notify_one ();
}

// The next block hash to iterate over, the priority is as follows:
// 1 - The next block in the account chain for the last processed receive (if there is any)
// 2 - The next receive block which is closest to genesis
// 3 - The last checkpoint hit.
// 4 - The hash that was passed in originally. Either all checkpoints were exhausted (this can happen when there are many accounts to genesis)
//     or all other blocks have been processed.
nano::confirmation_height_processor::top_and_next_hash nano::confirmation_height_processor::get_next_block (boost::optional<top_and_next_hash> const & next_in_receive_chain_a, boost::circular_buffer_space_optimized<nano::block_hash> const & checkpoints_a, boost::circular_buffer_space_optimized<receive_source_pair> const & receive_source_pairs, boost::optional<receive_chain_details> & receive_details_a)
{
	top_and_next_hash next;
	if (next_in_receive_chain_a.is_initialized ())
	{
		next = *next_in_receive_chain_a;
	}
	else if (!receive_source_pairs.empty ())
	{
		auto next_receive_source_pair = receive_source_pairs.back ();
		receive_details_a = next_receive_source_pair.receive_details;
		next = { next_receive_source_pair.source_hash, receive_details_a->next, receive_details_a->height + 1 };
	}
	else if (!checkpoints_a.empty ())
	{
		next = { checkpoints_a.back (), boost::none, 0 };
	}
	else
	{
		next = { original_hash, boost::none, 0 };
	}

	return next;
}

nano::block_hash nano::confirmation_height_processor::get_least_unconfirmed_hash_from_top_level (nano::transaction const & transaction_a, nano::block_hash const & hash_a, nano::account const & account_a, nano::confirmation_height_info const & confirmation_height_info_a, uint64_t & block_height_a)
{
	nano::block_hash least_unconfirmed_hash = hash_a;
	nano::block_sideband sideband;
	if (confirmation_height_info_a.height != 0)
	{
		if (block_height_a > confirmation_height_info_a.height)
		{
			release_assert (ledger.store.block_get (transaction_a, confirmation_height_info_a.frontier, &sideband) != nullptr);
			least_unconfirmed_hash = sideband.successor;
			block_height_a = sideband.height + 1;
		}
	}
	else
	{
		// No blocks have been confirmed, so the first block will be the open block
		nano::account_info account_info;
		release_assert (!ledger.store.account_get (transaction_a, account_a, account_info));
		least_unconfirmed_hash = account_info.open_block;
		block_height_a = 1;
	}
	return least_unconfirmed_hash;
}

void nano::confirmation_height_processor::set_next_hash ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	assert (!awaiting_processing.empty ());
	original_hash = *awaiting_processing.begin ();
	original_hashes_pending.insert (original_hash);
	awaiting_processing.erase (original_hash);
}

void nano::confirmation_height_processor::process ()
{
	nano::block_sideband sideband;
	nano::confirmation_height_info confirmation_height_info;
	auto transaction (ledger.store.tx_begin_read ());

	boost::optional<top_and_next_hash> next_in_receive_chain;
	boost::circular_buffer_space_optimized<nano::block_hash> checkpoints{ max_items };
	boost::circular_buffer_space_optimized<receive_source_pair> receive_source_pairs{ max_items };
	nano::block_hash current;
	do
	{
		boost::optional<receive_chain_details> receive_details;
		auto hash_to_process = get_next_block (next_in_receive_chain, checkpoints, receive_source_pairs, receive_details);
		current = hash_to_process.top;

		auto top_level_hash = current;
		nano::account account (ledger.store.block_account (transaction, current));
		release_assert (!ledger.store.confirmation_height_get (transaction, account, confirmation_height_info));

		// Checks if we have encountered this account before but not commited changes yet, if so then update the cached confirmation height
		auto account_it = accounts_confirmed_info.find (account);
		if (account_it != accounts_confirmed_info.cend () && account_it->second.confirmed_height > confirmation_height_info.height)
		{
			confirmation_height_info.height = account_it->second.confirmed_height;
			confirmation_height_info.frontier = account_it->second.iterated_frontier;
		}

		nano::block_sideband sideband;
		auto block = ledger.store.block_get (transaction, current, &sideband);
		assert (block != nullptr);
		auto block_height = sideband.height;
		bool already_cemented = confirmation_height_info.height >= block_height;

		// If we are not already at the bottom of the account chain (1 above cemented frontier) then find it
		if (!already_cemented && block_height - confirmation_height_info.height > 1)
		{
			if (block_height - confirmation_height_info.height == 2)
			{
				// If there is 1 uncemented block in-between this block and the cemented frontier,
				// we can just use the previous block to get the least unconfirmed hash.
				current = block->previous ();
				--block_height;
			}
			else if (!next_in_receive_chain.is_initialized ())
			{
				current = get_least_unconfirmed_hash_from_top_level (transaction, current, account, confirmation_height_info, block_height);
			}
			else
			{
				// Use the cached successor of the last receive which saves having to do more IO in get_least_unconfirmed_hash_from_top_level
				// as we already know what the next block we should process should be.
				current = *hash_to_process.next;
				block_height = hash_to_process.next_height;
			}
		}

		auto top_most_non_receive_block_hash = current;

		bool hit_receive = false;
		if (!already_cemented)
		{
			hit_receive = iterate (transaction, block_height, current, checkpoints, top_most_non_receive_block_hash, top_level_hash, receive_source_pairs, account);
		}

		// Exit early when the processor has been stopped, otherwise this function may take a
		// while (and hence keep the process running) if updating a long chain.
		if (stopped)
		{
			break;
		}

		// next_in_receive_chain can be modified when writing, so need to cache it here before resetting
		auto is_set = next_in_receive_chain.is_initialized ();
		next_in_receive_chain = boost::none;

		// Need to also handle the case where we are hitting receives where the sends below should be confirmed
		if (!hit_receive || (receive_source_pairs.size () == 1 && top_most_non_receive_block_hash != current))
		{
			preparation_data preparation_data{ transaction, top_most_non_receive_block_hash, already_cemented, checkpoints, account_it, confirmation_height_info, account, block_height, current, receive_details, next_in_receive_chain };
			prepare_iterated_blocks_for_cementing (preparation_data);

			// If used the top level, don't pop off the receive source pair because it wasn't used
			if (!is_set && !receive_source_pairs.empty ())
			{
				receive_source_pairs.pop_back ();
			}

			auto total_pending_write_block_count = std::accumulate (pending_writes.cbegin (), pending_writes.cend (), uint64_t (0), [](uint64_t total, auto const & write_details_a) {
				return total += write_details_a.top_height - write_details_a.bottom_height + 1;
			});

			auto max_batch_write_size_reached = (total_pending_write_block_count >= batch_write_size);
			// When there are a lot of pending confirmation height blocks, it is more efficient to
			// bulk some of them up to enable better write performance which becomes the bottleneck.
			auto min_time_exceeded = (timer.since_start () >= batch_separate_pending_min_time);
			auto finished_iterating = current == original_hash;
			auto non_awaiting_processing = awaiting_processing_size () == 0;
			auto should_output = finished_iterating && (non_awaiting_processing || min_time_exceeded);
			auto force_write = pending_writes.size () >= pending_writes_max_size || accounts_confirmed_info.size () >= pending_writes_max_size;

			if (((max_batch_write_size_reached || should_output) && !pending_writes.empty ()) || force_write)
			{
				bool error = false;
				// If nothing is currently using the database write lock then write the cemented pending blocks otherwise continue iterating
				if (write_database_queue.process (nano::writer::confirmation_height))
				{
					auto scoped_write_guard = write_database_queue.pop ();
					error = cement_blocks ();
				}
				else if (force_write)
				{
					auto scoped_write_guard = write_database_queue.wait (nano::writer::confirmation_height);
					error = cement_blocks ();
				}
				// Don't set any more cemented blocks from the original hash if an inconsistency is found
				if (error)
				{
					checkpoints.clear ();
					break;
				}
			}
		}

		transaction.refresh ();
	} while (!receive_source_pairs.empty () || current != original_hash);

	assert (checkpoints.empty ());
}

bool nano::confirmation_height_processor::iterate (nano::read_transaction const & transaction_a, uint64_t bottom_height_a, nano::block_hash const & bottom_hash_a, boost::circular_buffer_space_optimized<nano::block_hash> & checkpoints_a, nano::block_hash & top_most_non_receive_block_hash_a, nano::block_hash const & top_level_hash_a, boost::circular_buffer_space_optimized<receive_source_pair> & receive_source_pairs_a, nano::account const & account_a)
{
	bool reached_target = false;
	bool hit_receive = false;
	auto hash = bottom_hash_a;
	nano::block_sideband sideband;
	uint64_t num_blocks = 0;
	while (!hash.is_zero () && !reached_target && !stopped)
	{
		// Keep iterating upwards until we either reach the desired block or the second receive.
		// Once a receive is cemented, we can cement all blocks above it until the next receive, so store those details for later.
		++num_blocks;
		auto block = ledger.store.block_get (transaction_a, hash, &sideband);
		auto source (block->source ());
		if (source.is_zero ())
		{
			source = block->link ();
		}

		if (!source.is_zero () && !ledger.is_epoch_link (source) && ledger.store.source_exists (transaction_a, source))
		{
			hit_receive = true;
			reached_target = true;
			auto next = !sideband.successor.is_zero () && sideband.successor != top_level_hash_a ? boost::optional<nano::block_hash> (sideband.successor) : boost::none;
			receive_source_pairs_a.push_back (receive_source_pair{ receive_chain_details{ account_a, sideband.height, hash, top_level_hash_a, next, bottom_height_a, bottom_hash_a }, source });
			// Store a checkpoint every max_items so that we can always traverse a long number of accounts to genesis
			if (receive_source_pairs_a.size () % max_items == 0)
			{
				checkpoints_a.push_back (top_level_hash_a);
			}
		}
		else
		{
			// Found a send/change/epoch block which isn't the desired top level
			top_most_non_receive_block_hash_a = hash;
			if (hash == top_level_hash_a)
			{
				reached_target = true;
			}
			else
			{
				hash = sideband.successor;
			}
		}

		// We could be traversing a very large account so we don't want to open read transactions for too long.
		if ((num_blocks > 0) && num_blocks % batch_read_size == 0)
		{
			transaction_a.refresh ();
		}
	}

	return hit_receive;
}

// Once the path to genesis has been iterated to, we can begin to cement the lowest blocks in the accounts. This sets up
// the non-receive blocks which have been iterated for an account, and the associated receive block.
void nano::confirmation_height_processor::prepare_iterated_blocks_for_cementing (preparation_data & preparation_data_a)
{
	if (!preparation_data_a.already_cemented)
	{
		// Add the non-receive blocks iterated for this account
		auto block_height = (ledger.store.block_account_height (preparation_data_a.transaction, preparation_data_a.top_most_non_receive_block_hash));
		if (block_height > preparation_data_a.confirmation_height_info.height)
		{
			confirmed_info confirmed_info_l{ block_height, preparation_data_a.top_most_non_receive_block_hash };
			if (preparation_data_a.account_it != accounts_confirmed_info.cend ())
			{
				preparation_data_a.account_it->second = confirmed_info_l;
			}
			else
			{
				accounts_confirmed_info.emplace (preparation_data_a.account, confirmed_info_l);
				accounts_confirmed_info_size = accounts_confirmed_info.size ();
			}

			preparation_data_a.checkpoints.erase (std::remove (preparation_data_a.checkpoints.begin (), preparation_data_a.checkpoints.end (), preparation_data_a.top_most_non_receive_block_hash), preparation_data_a.checkpoints.end ());
			pending_writes.emplace_back (preparation_data_a.account, preparation_data_a.bottom_height, preparation_data_a.bottom_most, block_height, preparation_data_a.top_most_non_receive_block_hash);
			++pending_writes_size;
		}
	}

	// Add the receive block and all non-receive blocks above that one
	auto & receive_details = preparation_data_a.receive_details;
	if (receive_details)
	{
		auto receive_confirmed_info_it = accounts_confirmed_info.find (receive_details->account);
		if (receive_confirmed_info_it != accounts_confirmed_info.cend ())
		{
			auto & receive_confirmed_info = receive_confirmed_info_it->second;
			receive_confirmed_info.confirmed_height = receive_details->height;
			receive_confirmed_info.iterated_frontier = receive_details->hash;
		}
		else
		{
			accounts_confirmed_info.emplace (receive_details->account, confirmed_info{ receive_details->height, receive_details->hash });
			accounts_confirmed_info_size = accounts_confirmed_info.size ();
		}

		if (receive_details->next.is_initialized ())
		{
			preparation_data_a.next_in_receive_chain = top_and_next_hash{ receive_details->top_level, receive_details->next, receive_details->height + 1 };
		}
		else
		{
			preparation_data_a.checkpoints.erase (std::remove (preparation_data_a.checkpoints.begin (), preparation_data_a.checkpoints.end (), receive_details->hash), preparation_data_a.checkpoints.end ());
		}

		pending_writes.emplace_back (receive_details->account, receive_details->bottom_height, receive_details->bottom_most, receive_details->height, receive_details->hash);
		++pending_writes_size;
	}
}

bool nano::confirmation_height_processor::cement_blocks ()
{
	// Will contain all blocks that have been cemented (bounded by batch_write_size)
	// and will get run through the cemented observer callback
	std::vector<callback_data> cemented_blocks;
	{
		// This only writes to the confirmation_height table and is the only place to do so in a single process
		auto transaction (ledger.store.tx_begin_write ({}, { nano::tables::confirmation_height }));

		// Cement all pending entries, each entry is specific to an account and contains the least amount
		// of blocks to retain consistent cementing across all account chains to genesis.
		while (!pending_writes.empty ())
		{
			const auto & pending = pending_writes.front ();
			const auto & account = pending.account;

			auto write_confirmation_height = [&account, &ledger = ledger, &transaction](uint64_t num_blocks_cemented, uint64_t confirmation_height, nano::block_hash const & confirmed_frontier) {
#ifndef NDEBUG
				// Extra debug checks
				nano::confirmation_height_info confirmation_height_info;
				assert (!ledger.store.confirmation_height_get (transaction, account, confirmation_height_info));
				nano::block_sideband sideband;
				assert (ledger.store.block_get (transaction, confirmed_frontier, &sideband));
				assert (sideband.height == confirmation_height_info.height + num_blocks_cemented);
#endif
				ledger.store.confirmation_height_put (transaction, account, nano::confirmation_height_info{ confirmation_height, confirmed_frontier });
				ledger.cache.cemented_count += num_blocks_cemented;
				ledger.stats.add (nano::stat::type::confirmation_height, nano::stat::detail::blocks_confirmed, nano::stat::dir::in, num_blocks_cemented);
			};

			nano::confirmation_height_info confirmation_height_info;
			release_assert (!ledger.store.confirmation_height_get (transaction, pending.account, confirmation_height_info));

			// Some blocks need to be cemented at least
			if (pending.top_height > confirmation_height_info.height)
			{
				nano::block_sideband sideband;
				// The highest hash which will be cemented
				nano::block_hash new_cemented_frontier;
				uint64_t num_blocks_confirmed = 0;
				uint64_t start_height = 0;
				if (pending.bottom_height > confirmation_height_info.height)
				{
					new_cemented_frontier = pending.bottom_hash;
					// If we are higher than the cemented frontier, we should be exactly 1 block above
					assert (pending.bottom_height == confirmation_height_info.height + 1);
					num_blocks_confirmed = pending.top_height - pending.bottom_height + 1;
					start_height = pending.bottom_height;
				}
				else
				{
					auto block = ledger.store.block_get (transaction, confirmation_height_info.frontier, &sideband);
					new_cemented_frontier = sideband.successor;
					num_blocks_confirmed = pending.top_height - confirmation_height_info.height;
					start_height = confirmation_height_info.height + 1;
				}

				auto total_blocks_cemented = 0;
				auto num_blocks_iterated = 0;

				auto block = ledger.store.block_get (transaction, new_cemented_frontier, &sideband);

				// Cementing starts from the bottom of the chain and works upwards. This is because chains can have effectively
				// an infinite number of send/change blocks in a row. We don't want to hold the write transaction open for too long.
				for (; num_blocks_confirmed - num_blocks_iterated != 0; ++num_blocks_iterated)
				{
					if (!block)
					{
						logger.always_log ("Failed to write confirmation height for: ", new_cemented_frontier.to_string ());
						ledger.stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::invalid_block);
						pending_writes.clear ();
						pending_writes_size = 0;
						return true;
					}

					cemented_blocks.emplace_back (block, sideband);

					// We have likely hit a long chain, flush these callbacks and continue
					if (cemented_blocks.size () == batch_write_size)
					{
						auto num_blocks_cemented = num_blocks_iterated - total_blocks_cemented + 1;
						total_blocks_cemented += num_blocks_cemented;
						write_confirmation_height (num_blocks_cemented, start_height + total_blocks_cemented - 1, new_cemented_frontier);
						transaction.commit ();
						notify_observers (cemented_blocks);
						cemented_blocks.clear ();
						transaction.renew ();
					}

					// Get the next block in the chain until we have reached the final desired one
					auto last_iteration = (num_blocks_confirmed - num_blocks_iterated) == 1;
					if (!last_iteration)
					{
						new_cemented_frontier = sideband.successor;
						block = ledger.store.block_get (transaction, new_cemented_frontier, &sideband);
					}
					else
					{
						// Confirm it is indeed the last one
						assert (new_cemented_frontier == pending.top_hash);
					}
				}

				auto num_blocks_cemented = num_blocks_confirmed - total_blocks_cemented;
				write_confirmation_height (num_blocks_cemented, pending.top_height, new_cemented_frontier);
			}

			auto it = accounts_confirmed_info.find (pending.account);
			if (it != accounts_confirmed_info.cend () && it->second.confirmed_height == pending.top_height)
			{
				accounts_confirmed_info.erase (pending.account);
				accounts_confirmed_info_size = accounts_confirmed_info.size ();
			}
			pending_writes.pop_front ();
			--pending_writes_size;
		}
	}

	notify_observers (cemented_blocks);

	assert (pending_writes.empty ());
	assert (pending_writes_size == 0);
	nano::lock_guard<std::mutex> guard (mutex);
	original_hashes_pending.clear ();
	return false;
}

void nano::confirmation_height_processor::add_cemented_observer (std::function<void(callback_data)> const & callback_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	cemented_observers.push_back (callback_a);
}

void nano::confirmation_height_processor::add_cemented_batch_finished_observer (std::function<void()> const & callback_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	cemented_batch_finished_observers.push_back (callback_a);
}

void nano::confirmation_height_processor::notify_observers (std::vector<callback_data> const & cemented_blocks)
{
	for (auto const & block_callback_data : cemented_blocks)
	{
		for (auto const & observer : cemented_observers)
		{
			observer (block_callback_data);
		}
	}

	if (!cemented_blocks.empty ())
	{
		for (auto const & observer : cemented_batch_finished_observers)
		{
			observer ();
		}
	}
}

nano::confirmation_height_processor::receive_chain_details::receive_chain_details (nano::account const & account_a, uint64_t height_a, nano::block_hash const & hash_a, nano::block_hash const & top_level_a, boost::optional<nano::block_hash> next_a, uint64_t bottom_height_a, nano::block_hash const & bottom_most_a) :
account (account_a),
height (height_a),
hash (hash_a),
top_level (top_level_a),
next (next_a),
bottom_height (bottom_height_a),
bottom_most (bottom_most_a)
{
}

nano::confirmation_height_processor::write_details::write_details (nano::account const & account_a, uint64_t bottom_height_a, nano::block_hash const & bottom_hash_a, uint64_t top_height_a, nano::block_hash const & top_hash_a) :
bottom_height (bottom_height_a),
bottom_hash (bottom_hash_a),
top_height (top_height_a),
top_hash (top_hash_a),
account (account_a)
{
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (confirmation_height_processor & confirmation_height_processor_a, const std::string & name_a)
{
	auto composite = std::make_unique<container_info_composite> (name_a);

	size_t cemented_observers_count;
	size_t cemented_batch_finished_observer_count;
	{
		nano::lock_guard<std::mutex> guard (confirmation_height_processor_a.mutex);
		cemented_observers_count = confirmation_height_processor_a.cemented_observers.size ();
		cemented_batch_finished_observer_count = confirmation_height_processor_a.cemented_batch_finished_observers.size ();
	}

	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented_observers", cemented_observers_count, sizeof (decltype (confirmation_height_processor_a.cemented_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cemented_batch_finished_observers", cemented_batch_finished_observer_count, sizeof (decltype (confirmation_height_processor_a.cemented_batch_finished_observers)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pending_writes", confirmation_height_processor_a.pending_writes_size, sizeof (decltype (confirmation_height_processor_a.pending_writes)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "accounts_confirmed_info", confirmation_height_processor_a.accounts_confirmed_info_size, sizeof (decltype (confirmation_height_processor_a.accounts_confirmed_info)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "awaiting_processing", confirmation_height_processor_a.awaiting_processing_size (), sizeof (decltype (confirmation_height_processor_a.awaiting_processing)::value_type) }));
	return composite;
}

size_t nano::confirmation_height_processor::awaiting_processing_size ()
{
	nano::lock_guard<std::mutex> guard (mutex);
	return awaiting_processing.size ();
}

bool nano::confirmation_height_processor::is_processing_block (nano::block_hash const & hash_a)
{
	nano::lock_guard<std::mutex> guard (mutex);
	return original_hashes_pending.find (hash_a) != original_hashes_pending.cend () || awaiting_processing.find (hash_a) != awaiting_processing.cend ();
}

nano::block_hash nano::confirmation_height_processor::current ()
{
	nano::lock_guard<std::mutex> lk (mutex);
	return original_hash;
}

nano::confirmation_height_processor::receive_source_pair::receive_source_pair (confirmation_height_processor::receive_chain_details const & receive_details_a, const block_hash & source_a) :
receive_details (receive_details_a),
source_hash (source_a)
{
}

nano::confirmation_height_processor::confirmed_info::confirmed_info (uint64_t confirmed_height_a, nano::block_hash const & iterated_frontier_a) :
confirmed_height (confirmed_height_a),
iterated_frontier (iterated_frontier_a)
{
}

nano::confirmation_height_processor::callback_data::callback_data (std::shared_ptr<nano::block> const & block_a, nano::block_sideband const & sideband_a) :
block (block_a),
sideband (sideband_a)
{
}
