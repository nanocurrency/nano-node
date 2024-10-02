#include <nano/lib/thread_roles.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (confirming_set_config const & config_a, nano::ledger & ledger_a, nano::stats & stats_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	notification_workers{ 1, nano::thread_role::name::confirmation_height_notifications }
{
	batch_cemented.add ([this] (auto const & notification) {
		for (auto const & [block, confirmation_root] : notification.cemented)
		{
			cemented_observers.notify (block);
		}
	});
}

nano::confirming_set::~confirming_set ()
{
	debug_assert (!thread.joinable ());
}

void nano::confirming_set::add (nano::block_hash const & hash)
{
	bool added = false;
	{
		std::lock_guard lock{ mutex };
		auto [it, inserted] = set.insert (hash);
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

bool nano::confirming_set::exists (nano::block_hash const & hash) const
{
	std::lock_guard lock{ mutex };
	return set.count (hash) != 0;
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

std::deque<nano::block_hash> nano::confirming_set::next_batch (size_t max_count)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<nano::block_hash> results;
	while (!set.empty () && results.size () < max_count)
	{
		auto it = set.begin ();
		results.push_back (*it);
		set.erase (it);
	}
	return results;
}

void nano::confirming_set::run_batch (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<cemented_t> cemented;
	std::deque<nano::block_hash> already;

	auto batch = next_batch (256);

	lock.unlock ();

	auto notify = [this, &cemented, &already] () {
		cemented_notification notification{};
		notification.cemented.swap (cemented);
		notification.already_cemented.swap (already);

		std::unique_lock lock{ mutex };

		while (notification_workers.num_queued_tasks () >= config.max_queued_notifications)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cooldown);
			condition.wait_for (lock, 100ms, [this] { return stopped.load (); });
			if (stopped)
			{
				return;
			}
		}

		notification_workers.push_task ([this, notification = std::move (notification)] () {
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify);
			batch_cemented.notify (notification);
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
		for (auto const & hash : batch)
		{
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

				auto added = ledger.confirm (transaction, hash, config.max_blocks);
				if (!added.empty ())
				{
					// Confirming this block may implicitly confirm more
					stats.add (nano::stat::type::confirming_set, nano::stat::detail::cemented, added.size ());
					for (auto & block : added)
					{
						cemented.emplace_back (block, hash);
					}
				}
				else
				{
					stats.inc (nano::stat::type::confirming_set, nano::stat::detail::already_cemented);
					already.push_back (hash);
					debug_assert (ledger.confirmed.block_exists (transaction, hash));
				}
			} while (!ledger.confirmed.block_exists (transaction, hash));

			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::cemented_hash);
		}
	}

	notify ();

	release_assert (cemented.empty ());
	release_assert (already.empty ());
}

nano::container_info nano::confirming_set::container_info () const
{
	std::lock_guard guard{ mutex };

	nano::container_info info;
	info.put ("set", set);
	info.add ("notification_workers", notification_workers.container_info ());
	return info;
}
