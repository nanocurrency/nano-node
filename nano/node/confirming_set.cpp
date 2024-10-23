#include <nano/lib/logging.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (confirming_set_config const & config_a, nano::ledger & ledger_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	logger{ logger_a },
	workers{ 1, nano::thread_role::name::confirmation_height_notifications }
{
	batch_cemented.add ([this] (auto const & cemented) {
		for (auto const & context : cemented)
		{
			cemented_observers.notify (context.block);
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

	workers.start ();

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
	workers.stop ();
}

bool nano::confirming_set::contains (nano::block_hash const & hash) const
{
	std::lock_guard lock{ mutex };
	return set.get<tag_hash> ().contains (hash) || current.contains (hash);
}

std::size_t nano::confirming_set::size () const
{
	std::lock_guard lock{ mutex };
	return set.size ();
}

void nano::confirming_set::run ()
{
	std::unique_lock lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::loop);

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
	for (auto const & [hash, election] : batch)
	{
		current.insert (hash);
	}

	lock.unlock ();

	auto notify = [this, &cemented] () {
		std::deque<context> batch;
		batch.swap (cemented);

		std::unique_lock lock{ mutex };

		// It's possible that ledger cementing happens faster than the notifications can be processed by other components, cooldown here
		while (workers.queued_tasks () >= config.max_queued_notifications)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cooldown);
			condition.wait_for (lock, 100ms, [this] { return stopped.load (); });
			if (stopped)
			{
				return;
			}
		}

		workers.post ([this, batch = std::move (batch)] () {
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
		for (auto const & [hash, election] : batch)
		{
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
			}
		}
	}

	notify ();
	release_assert (cemented.empty ());

	already_cemented.notify (already);

	lock.lock ();
	current.clear ();
	lock.unlock ();
}

nano::container_info nano::confirming_set::container_info () const
{
	std::lock_guard guard{ mutex };

	nano::container_info info;
	info.put ("set", set);
	info.put ("notifications", workers.queued_tasks ());
	info.add ("workers", workers.container_info ());
	return info;
}
