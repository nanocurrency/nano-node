#include <nano/lib/threading.hpp>
#include <nano/node/backlog_population.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/store.hpp>

nano::backlog_population::backlog_population (const config & config_a, nano::store & store_a, nano::stats & stats_a) :
	config_m{ config_a },
	store{ store_a },
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
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
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
	return triggered || config_m.enabled;
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
			populate_backlog (lock);
		}

		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::backlog_population::populate_backlog (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (config_m.frequency > 0);

	const auto chunk_size = config_m.batch_size / config_m.frequency;
	auto done = false;
	nano::account next = 0;
	uint64_t total = 0;
	while (!stopped && !done)
	{
		lock.unlock ();

		{
			auto transaction = store.tx_begin_read ();

			auto count = 0u;
			auto i = store.account.begin (transaction, next);
			auto const end = store.account.end ();
			for (; i != end && count < chunk_size; ++i, ++count, ++total)
			{
				stats.inc (nano::stat::type::backlog, nano::stat::detail::total);

				auto const & account = i->first;
				activate (transaction, account);
				next = account.number () + 1;
			}
			done = store.account.begin (transaction, next) == end;
		}

		lock.lock ();

		// Give the rest of the node time to progress without holding database lock
		condition.wait_for (lock, std::chrono::milliseconds{ 1000 / config_m.frequency });
	}
}

void nano::backlog_population::activate (nano::transaction const & transaction, nano::account const & account)
{
	debug_assert (!activate_callback.empty ());

	auto const maybe_account_info = store.account.get (transaction, account);
	if (!maybe_account_info)
	{
		return;
	}
	auto const account_info = *maybe_account_info;

	auto const maybe_conf_info = store.confirmation_height.get (transaction, account);
	auto const conf_info = maybe_conf_info.value_or (nano::confirmation_height_info{});

	// If conf info is empty then it means then it means nothing is confirmed yet
	if (conf_info.height < account_info.block_count)
	{
		stats.inc (nano::stat::type::backlog, nano::stat::detail::activated);

		activate_callback.notify (transaction, account, account_info, conf_info);
	}
}
