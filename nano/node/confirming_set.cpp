#include "node.hpp"

#include <nano/lib/thread_roles.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (nano::ledger & ledger_a, nano::stats & stats_a, std::chrono::milliseconds batch_time_a) :
	ledger{ ledger_a },
	stats{ stats_a },
	batch_time{ batch_time_a },
	notification_workers{ 1, nano::thread_role::name::confirmation_height_notifications }
{
	batch_cemented.add ([this] (auto const & notification) {
		for (auto const & [block, confirmation_root] : notification.cemented)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify_cemented);
			cemented_observers.notify (block);
		}
		for (auto const & hash : notification.already_cemented)
		{
			stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify_already_cemented);
			block_already_cemented_observers.notify (hash);
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
	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::confirmation_height_processing);
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
	return set.count (hash) != 0 || processing.count (hash) != 0;
}

std::size_t nano::confirming_set::size () const
{
	std::lock_guard lock{ mutex };
	return set.size () + processing.size ();
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
			debug_assert (lock.owns_lock ());
		}
		else
		{
			condition.wait (lock, [&] () { return !set.empty () || stopped; });
		}
	}
}

void nano::confirming_set::run_batch (std::unique_lock<std::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());
	debug_assert (!set.empty ());

	std::deque<cemented_t> cemented;
	std::deque<nano::block_hash> already;

	// Move items in to back buffer and release lock so more items can be added to the front buffer
	release_assert (processing.empty ());
	swap (set, processing);

	// Process all items in the back buffer
	for (auto i = processing.begin (), n = processing.end (); !stopped && i != n;)
	{
		lock.unlock (); // Waiting for db write is potentially slow

		auto guard = ledger.store.write_queue.wait (nano::store::writer::confirmation_height);
		auto tx = ledger.tx_begin_write ({ nano::tables::confirmation_height });

		lock.lock ();
		// Process items in the back buffer within a single transaction for a limited amount of time
		for (auto timeout = std::chrono::steady_clock::now () + batch_time; !stopped && std::chrono::steady_clock::now () < timeout && i != n; ++i)
		{
			auto item = *i;
			lock.unlock ();

			auto added = ledger.confirm (tx, item);
			if (!added.empty ())
			{
				// Confirming this block may implicitly confirm more
				for (auto & block : added)
				{
					cemented.emplace_back (block, item);
				}

				stats.add (nano::stat::type::confirming_set, nano::stat::detail::cemented, added.size ());
			}
			else
			{
				already.push_back (item);
				stats.inc (nano::stat::type::confirming_set, nano::stat::detail::already_confirmed);
			}

			lock.lock ();
		}
	}

	lock.unlock ();

	cemented_notification notification{
		.cemented = std::move (cemented),
		.already_cemented = std::move (already)
	};

	notification_workers.push_task ([this, notification = std::move (notification)] () {
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify);
		batch_cemented.notify (notification);
	});

	lock.lock ();

	processing.clear ();
}

std::unique_ptr<nano::container_info_component> nano::confirming_set::collect_container_info (std::string const & name) const
{
	std::lock_guard guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "set", set.size (), sizeof (typename decltype (set)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "processing", processing.size (), sizeof (typename decltype (processing)::value_type) }));
	composite->add_component (notification_workers.collect_container_info ("notification_workers"));
	return composite;
}
