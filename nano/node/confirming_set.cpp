#include <nano/lib/thread_roles.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (nano::ledger & ledger_a, nano::stats & stats_a) :
	ledger{ ledger_a },
	stats{ stats_a },
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
	debug_assert (!thread.joinable ());

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

	{
		auto transaction = ledger.tx_begin_write ({ nano::tables::confirmation_height }, nano::store::writer::confirmation_height);

		for (auto const & hash : batch)
		{
			transaction.refresh_if_needed ();

			auto added = ledger.confirm (transaction, hash);
			if (!added.empty ())
			{
				// Confirming this block may implicitly confirm more
				for (auto & block : added)
				{
					cemented.emplace_back (block, hash);
				}

				stats.add (nano::stat::type::confirming_set, nano::stat::detail::cemented, added.size ());
			}
			else
			{
				already.push_back (hash);
				stats.inc (nano::stat::type::confirming_set, nano::stat::detail::already_cemented);
			}
		}
	}

	cemented_notification notification{
		.cemented = std::move (cemented),
		.already_cemented = std::move (already)
	};

	notification_workers.push_task ([this, notification = std::move (notification)] () {
		stats.inc (nano::stat::type::confirming_set, nano::stat::detail::notify);
		batch_cemented.notify (notification);
	});
}

std::unique_ptr<nano::container_info_component> nano::confirming_set::collect_container_info (std::string const & name) const
{
	std::lock_guard guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "set", set.size (), sizeof (typename decltype (set)::value_type) }));
	composite->add_component (notification_workers.collect_container_info ("notification_workers"));
	return composite;
}
