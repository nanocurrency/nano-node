#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/transaction.hpp>

nano::ledger_notifications::ledger_notifications (nano::node_config const & config, nano::stats & stats, nano::logger & logger) :
	config{ config },
	stats{ stats },
	logger{ logger }
{
}

nano::ledger_notifications::~ledger_notifications ()
{
	debug_assert (!thread.joinable ());
}

void nano::ledger_notifications::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::ledger_notifications);
		run ();
	} };
}

void nano::ledger_notifications::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}

	// Notifications still queued are dropped, which breaks the result promises of the processed blocks they carry
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stats.add (nano::stat::type::ledger_notifications, nano::stat::detail::stopped, notifications.size ());
		notifications.clear ();
	}
}

void nano::ledger_notifications::wait (std::function<void ()> cooldown_action)
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this, &cooldown_action] {
		bool predicate = stopped || notifications.size () < config.max_ledger_notifications;
		if (!predicate && cooldown_action)
		{
			cooldown_action ();
		}
		return predicate;
	});
}

void nano::ledger_notifications::notify_processed (nano::secure::write_transaction & transaction, processed_batch_t processed, std::function<void ()> callback)
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		notifications.emplace_back (transaction.get_future (), nano::wrap_move_only ([this, processed = std::move (processed), callback = std::move (callback)] () mutable {
			stats.inc (nano::stat::type::ledger_notifications, nano::stat::detail::notify_processed);

			// Set results for futures when not holding the lock
			for (auto & [result, context] : processed)
			{
				if (context.callback)
				{
					context.callback (result);
				}
				context.set_result (result);
			}

			blocks_processed.notify (processed);

			if (callback)
			{
				callback ();
			}
		}));
	}
	condition.notify_all ();
}

void nano::ledger_notifications::notify_rolled_back (nano::secure::write_transaction & transaction, rolled_back_batch_t batch, nano::qualified_root rollback_root, std::function<void ()> callback)
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		notifications.emplace_back (transaction.get_future (), nano::wrap_move_only ([this, batch = std::move (batch), rollback_root, callback = std::move (callback)] () {
			stats.inc (nano::stat::type::ledger_notifications, nano::stat::detail::notify_rolled_back);

			blocks_rolled_back.notify (batch, rollback_root);

			if (callback)
			{
				callback ();
			}
		}));
	}
	condition.notify_all ();
}

void nano::ledger_notifications::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] {
			return stopped || !notifications.empty ();
		});

		if (stopped)
		{
			return;
		}

		while (!notifications.empty ())
		{
			auto notification = std::move (notifications.front ());
			notifications.pop_front ();
			lock.unlock ();

			auto & [future, callback] = notification;
			future.wait (); // Wait for the associated transaction to be committed
			callback (); // Notify observers

			condition.notify_all (); // Notify waiting threads about possible vacancy

			lock.lock ();
		}
	}
}

nano::container_info nano::ledger_notifications::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("notifications", notifications.size ());
	return info;
}