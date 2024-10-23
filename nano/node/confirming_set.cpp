#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (confirming_set_config const & config_a, nano::ledger & ledger_a, nano::block_processor & block_processor_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	block_processor{ block_processor_a },
	stats{ stats_a },
	logger{ logger_a },
	notification_workers{ 1, nano::thread_role::name::confirmation_height_notifications }
{
	batch_cemented.add ([this] (auto const & cemented) {
		for (auto const & context : cemented)
		{
			cemented_observers.notify (context.block);
		}
	});

	// Requeue blocks that failed to cement immediately due to missing ledger blocks
	block_processor.batch_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		{
			std::lock_guard lock{ mutex };
			for (auto const & [result, context] : batch)
			{
				if (auto it = deferred.get<tag_hash> ().find (context.block->hash ()); it != deferred.get<tag_hash> ().end ())
				{
					stats.inc (nano::stat::type::confirming_set, nano::stat::detail::requeued);
					set.push_back (*it);
					deferred.get<tag_hash> ().erase (it);
					should_notify = true;
				}
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});
}

nano::confirming_set::~confirming_set ()
{
	debug_assert (!thread.joinable ());
}

void nano::confirming_set::add (nano::block_hash const & hash, std::shared_ptr<nano::election> const & election)
{
	bool added = false;
	{
		std::lock_guard lock{ mutex };
		auto [it, inserted] = set.push_back ({ hash, election });
		added = inserted;
	}
	if (added)
	{
		condition.notify_all ();
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::insert);
	}
	else
	{
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::duplicate);
	}
}

void nano::confirming_set::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::confirmation_height);
		run ();
	} };
}

void nano::confirming_set::stop ()
{
	{
		std::lock_guard lock{ mutex };
		stopped = true;
		condition.notify_all ();
	}
	if (thread.joinable ())
	{
		thread.join ();
	}
	notification_workers.stop ();
}

bool nano::confirming_set::contains (nano::block_hash const & hash) const
{
	std::lock_guard lock{ mutex };
	return set.get<tag_hash> ().contains (hash) || deferred.get<tag_hash> ().contains (hash) || current.contains (hash);
}

std::size_t nano::confirming_set::size () const
{
	// Do not report deferred blocks, as they are not currently being processed (and might never be requeued)
	std::lock_guard lock{ mutex };
	return set.size () + current.size ();
}

void nano::confirming_set::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::loop);

		cleanup (lock);
		debug_assert (lock.owns_lock ());

		if (!set.empty ())
		{
			run_batch (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
		else
		{
			condition.wait (lock, [&] () { return !set.empty () || stopped; });
		}
	}
}

auto nano::confirming_set::next_batch (size_t max_count) -> std::deque<entry>
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<entry> results;
	while (!set.empty () && results.size () < max_count)
	{
		results.push_back (set.front ());
		set.pop_front ();
	}
	return results;
}

void nano::confirming_set::run_batch (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<context> cemented;
	std::deque<nano::block_hash> already;

	auto batch = next_batch (config.batch_size);

	// Keep track of the blocks we're currently cementing, so that the .contains (...) check is accurate
	debug_assert (current.empty ());
	for (auto const & entry : batch)
	{
		current.insert (entry.hash);
	}

	lock.unlock ();

	auto notify = [this, &cemented] () {
		std::deque<context> batch;
		batch.swap (cemented);

		std::unique_lock lock{ mutex };

		// It's possible that ledger cementing happens faster than the notifications can be processed by other components, cooldown here
		while (notification_workers.num_queued_tasks () >= config.max_queued_notifications)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cooldown);
			condition.wait_for (lock, 100ms, [this] { return stopped.load (); });
			if (stopped)
			{
				return;
			}
		}

		notification_workers.push_task ([this, batch = std::move (batch)] () {
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify);
			batch_cemented.notify (batch);
		});
	};

	// We might need to issue multiple notifications if the block we're confirming implicitly confirms more
	auto notify_maybe = [this, &cemented, &already, &notify] (auto & transaction) {
		if (cemented.size () >= config.max_blocks)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify_intermediate);
			transaction.commit ();
			notify ();
			transaction.renew ();
		}
	};

	{
		auto transaction = ledger.tx_begin_write (nano::store::writer::confirmation_height);
		for (auto const & entry : batch)
		{
			auto const & hash = entry.hash;
			auto const & election = entry.election;

			size_t cemented_count = 0;
			bool success = false;
			do
			{
				transaction.refresh_if_needed ();

				// Cementing deep dependency chains might take a long time, allow for graceful shutdown, ignore notifications
				if (stopped)
				{
					return;
				}

				// Issue notifications here, so that `cemented` set is not too large before we add more blocks
				notify_maybe (transaction);

				stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cementing);

				// The block might be rolled back before it's fully cemented
				if (!ledger.any.block_exists (transaction, hash))
				{
					stats.inc (nano::stat::type::confirming_set, nano::stat::detail::missing_block);
					break;
				}

				auto added = ledger.confirm (transaction, hash, config.max_blocks);
				if (!added.empty ())
				{
					// Confirming this block may implicitly confirm more
					stats.add (nano::stat::type::confirming_set, nano::stat::detail::cemented, added.size ());
					for (auto & block : added)
					{
						cemented.push_back ({ block, hash, election });
					}
					cemented_count += added.size ();
				}
				else
				{
					stats.inc (nano::stat::type::confirming_set, nano::stat::detail::already_cemented);
					already.push_back (hash);
					debug_assert (ledger.confirmed.block_exists (transaction, hash));
				}

				success = ledger.confirmed.block_exists (transaction, hash);
			} while (!success);

			if (success)
			{
				stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cemented_hash);
				logger.debug (nano::log::type::confirming_set, "Cemented block: {} (total cemented: {})", hash.to_string (), cemented_count);
			}
			else
			{
				stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cementing_failed);
				logger.debug (nano::log::type::confirming_set, "Failed to cement block: {}", hash.to_string ());

				// Requeue failed blocks for processing later
				// Add them to the deferred set while still holding the exclusive database write transaction to avoid block processor races
				lock.lock ();
				deferred.push_back (entry);
				lock.unlock ();
			}
		}
	}

	notify ();
	release_assert (cemented.empty ());

	already_cemented.notify (already);

	// Clear current set only after the transaction is committed
	lock.lock ();
	current.clear ();
	lock.unlock ();
}

void nano::confirming_set::cleanup (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	auto const cutoff = std::chrono::steady_clock::now () - config.deferred_age_cutoff;
	std::deque<entry> evicted;

	auto should_evict = [&] (entry const & entry) {
		return entry.timestamp < cutoff;
	};

	// Iterate in sequenced (insertion) order
	for (auto it = deferred.begin (), end = deferred.end (); it != end;)
	{
		if (should_evict (*it) || deferred.size () > config.max_deferred)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::evicted);
			debug_assert (ledger.any.block_exists (ledger.tx_begin_read (), it->hash));
			evicted.push_back (*it);
			it = deferred.erase (it);
		}
		else
		{
			break; // Entries are sequenced, so we can stop here and avoid unnecessary iteration
		}
	}

	// Notify about evicted blocks so that other components can perform necessary cleanup
	if (!evicted.empty ())
	{
		lock.unlock ();
		for (auto const & entry : evicted)
		{
			cementing_failed.notify (entry.hash);
		}
		lock.lock ();
	}
}

nano::container_info nano::confirming_set::container_info () const
{
	std::lock_guard guard{ mutex };

	nano::container_info info;
	info.put ("set", set);
	info.put ("deferred", deferred);
	info.add ("notification_workers", notification_workers.container_info ());
	return info;
}
