#include <nano/lib/threading.hpp>
#include <nano/node/backlog_population.hpp>
#include <nano/node/election_scheduler.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/secure/store.hpp>

nano::backlog_population::backlog_population (const config & config_a, nano::store & store_a, nano::election_scheduler & scheduler_a, nano::stat & stats_a) :
	config_m{ config_a },
	store{ store_a },
	scheduler{ scheduler_a },
	stats{ stats_a }
{
}

nano::backlog_population::~backlog_population ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::backlog_population::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::backlog_population);
		run ();
	} };
}

void nano::backlog_population::stop ()
{
	stopped = true;
	notify ();
	nano::join_or_pass (thread);
}

void nano::backlog_population::trigger ()
{
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		triggered = true;
	}
	notify ();
}

void nano::backlog_population::notify ()
{
	condition.notify_all ();
}

bool nano::backlog_population::predicate () const
{
	return triggered || config_m.ongoing_backlog_population_enabled;
}

void nano::backlog_population::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			stats.inc (nano::stat::type::backlog, nano::stat::detail::loop);

			triggered = false;
			lock.unlock ();
			populate_backlog ();
			lock.lock ();
		}

		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::backlog_population::populate_backlog ()
{
	auto done = false;
	nano::account next = 0;
	uint64_t total = 0;
	while (!stopped && !done)
	{
		{
			auto transaction = store.tx_begin_read ();

			auto count = 0;
			auto i = store.account.begin (transaction, next);
			const auto end = store.account.end ();
			for (; !stopped && i != end && count < chunk_size; ++i, ++count, ++total)
			{
				stats.inc (nano::stat::type::backlog, nano::stat::detail::total);

				auto const & account = i->first;
				bool activated = scheduler.activate (account, transaction);
				if (activated)
				{
					stats.inc (nano::stat::type::backlog, nano::stat::detail::activated);
				}

				next = account.number () + 1;
			}
			done = store.account.begin (transaction, next) == end;
		}

		// Give the rest of the node time to progress without holding database lock
		std::this_thread::sleep_for (chunk_interval);
	}
}