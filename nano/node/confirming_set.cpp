#include <nano/lib/thread_roles.hpp>
#include <nano/node/confirming_set.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/component.hpp>
#include <nano/store/write_queue.hpp>

nano::confirming_set::confirming_set (nano::ledger & ledger, std::chrono::milliseconds batch_time) :
	ledger{ ledger },
	batch_time{ batch_time }
{
}

nano::confirming_set::~confirming_set ()
{
	debug_assert (!thread.joinable ());
}

void nano::confirming_set::add (nano::block_hash const & hash)
{
	std::lock_guard lock{ mutex };
	set.insert (hash);
	condition.notify_all ();
}

void nano::confirming_set::start ()
{
	thread = std::thread{ [this] () { run (); } };
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
	nano::thread_role::set (nano::thread_role::name::confirmation_height_processing);
	std::unique_lock lock{ mutex };
	// Run the confirmation loop until stopped
	while (!stopped)
	{
		condition.wait (lock, [&] () { return !set.empty () || stopped; });
		// Loop if there are items to process
		if (!stopped && !set.empty ())
		{
			std::deque<std::shared_ptr<nano::block>> cemented;
			std::deque<nano::block_hash> already;
			// Move items in to back buffer and release lock so more items can be added to the front buffer
			processing = std::move (this->set);
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
					if (ledger.any.block_exists (tx, item))
					{
						auto added = ledger.confirm (tx, item);
						if (!added.empty ())
						{
							// Confirming this block may implicitly confirm more
							cemented.insert (cemented.end (), added.begin (), added.end ());
						}
						else
						{
							already.push_back (item);
						}
					}
					else
					{
						ledger.stats.inc (nano::stat::type::confirmation_height, nano::stat::detail::block_late_removed);
					}
					lock.lock ();
				}
			}
			lock.unlock ();
			for (auto const & i : cemented)
			{
				cemented_observers.notify (i);
			}
			for (auto const & i : already)
			{
				block_already_cemented_observers.notify (i);
			}
			lock.lock ();
			// Clear and free back buffer by re-initializing
			processing = decltype (processing){};
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::confirming_set::collect_container_info (std::string const & name) const
{
	std::lock_guard guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "set", set.size (), sizeof (typename decltype (set)::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "processing", processing.size (), sizeof (typename decltype (processing)::value_type) }));
	return composite;
}
