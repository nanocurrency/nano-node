#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>

nano::backlog_scan::backlog_scan (backlog_scan_config const & config_a, nano::ledger & ledger_a, nano::stats & stats_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	limiter{ config.rate_limit }
{
}

nano::backlog_scan::~backlog_scan ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::backlog_scan::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::backlog_scan);
		run ();
	} };
}

void nano::backlog_scan::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::backlog_scan::trigger ()
{
	{
		nano::unique_lock<nano::mutex> lock{ mutex };
		triggered = true;
	}
	notify ();
}

void nano::backlog_scan::notify ()
{
	condition.notify_all ();
}

bool nano::backlog_scan::predicate () const
{
	return triggered || config.enable;
}

void nano::backlog_scan::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			stats.inc (nano::stat::type::backlog_scan, nano::stat::detail::loop);
			triggered = false;
			populate_backlog (lock); // Does a single iteration over all accounts
			debug_assert (lock.owns_lock ());
		}
		else
		{
			condition.wait (lock, [this] () {
				return stopped || predicate ();
			});
		}
	}
}

void nano::backlog_scan::populate_backlog (nano::unique_lock<nano::mutex> & lock)
{
	uint64_t total = 0;

	nano::account next = 0;
	bool done = false;
	while (!stopped && !done)
	{
		// Wait for the rate limiter
		while (!limiter.should_pass (config.batch_size))
		{
			std::chrono::milliseconds const wait_time{ 1000 / std::max ((config.rate_limit / config.batch_size), size_t{ 1 }) / 2 };
			condition.wait_for (lock, std::max (wait_time, 10ms));
			if (stopped)
			{
				return;
			}
		}

		lock.unlock ();

		std::deque<activated_info> scanned;
		std::deque<activated_info> activated;
		{
			auto transaction = ledger.tx_begin_read ();

			auto it = ledger.store.account.begin (transaction, next);
			auto const end = ledger.store.account.end (transaction);

			for (size_t count = 0; it != end && count < config.batch_size; ++it, ++count, ++total)
			{
				stats.inc (nano::stat::type::backlog_scan, nano::stat::detail::total);

				auto const [account, account_info] = *it;
				auto const maybe_conf_info = ledger.store.confirmation_height.get (transaction, account);
				auto const conf_info = maybe_conf_info.value_or (nano::confirmation_height_info{});

				activated_info info{ account, account_info, conf_info };

				scanned.push_back (info);
				if (conf_info.height < account_info.block_count)
				{
					activated.push_back (info);
				}

				next = inc_sat (account.number ());
			}

			done = (it == end);
		}

		stats.add (nano::stat::type::backlog_scan, nano::stat::detail::scanned, scanned.size ());
		stats.add (nano::stat::type::backlog_scan, nano::stat::detail::activated, activated.size ());

		// Notify about scanned and activated accounts without holding database transaction
		batch_scanned.notify (scanned);
		batch_activated.notify (activated);

		lock.lock ();
	}
}

nano::container_info nano::backlog_scan::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	nano::container_info info;
	info.put ("limiter", limiter.size ());
	return info;
}

/*
 * backlog_scan_config
 */

nano::error nano::backlog_scan_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC \ntype:bool");
	toml.put ("batch_size", batch_size, "Size of a single batch. Larger batches reduce overhead, but may put more pressure on other node components. \ntype:uint");
	toml.put ("rate_limit", rate_limit, "Number of accounts per second to process when doing backlog population scan. Increasing this value will help unconfirmed frontiers get into election prioritization queue faster. Use 0 to process as fast as possible, but be aware that it may consume a lot of resources. \ntype:uint");

	return toml.get_error ();
}

nano::error nano::backlog_scan_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("batch_size", batch_size);
	toml.get ("rate_limit", rate_limit);

	return toml.get_error ();
}
