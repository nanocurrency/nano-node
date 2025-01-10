#include <celerix/lib/thread_roles.hpp>
#include <celerix/lib/threading.hpp>
#include <celerix/node/backlog_scan.hpp>
#include <celerix/node/nodeconfig.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/store/account.hpp>
#include <celerix/store/component.hpp>
#include <celerix/store/confirmation_height.hpp>

celerix::backlog_scan::backlog_scan (backlog_scan_config const & config_a, celerix::ledger & ledger_a, celerix::stats & stats_a) :
	config{ config_a },
	ledger{ ledger_a },
	stats{ stats_a },
	limiter{ config.rate_limit }
{
}

celerix::backlog_scan::~backlog_scan ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void celerix::backlog_scan::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::backlog_scan);
		run ();
	} };
}

void celerix::backlog_scan::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	celerix::join_or_pass (thread);
}

void celerix::backlog_scan::trigger ()
{
	{
		celerix::unique_lock<celerix::mutex> lock{ mutex };
		triggered = true;
	}
	notify ();
}

void celerix::backlog_scan::notify ()
{
	condition.notify_all ();
}

bool celerix::backlog_scan::predicate () const
{
	return triggered || config.enable;
}

void celerix::backlog_scan::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			stats.inc (celerix::stat::type::backlog_scan, celerix::stat::detail::loop);
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

void celerix::backlog_scan::populate_backlog (celerix::unique_lock<celerix::mutex> & lock)
{
	uint64_t total = 0;

	celerix::account next = 0;
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
				stats.inc (celerix::stat::type::backlog_scan, celerix::stat::detail::total);

				auto const [account, account_info] = *it;
				auto const maybe_conf_info = ledger.store.confirmation_height.get (transaction, account);
				auto const conf_info = maybe_conf_info.value_or (celerix::confirmation_height_info{});

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

		stats.add (celerix::stat::type::backlog_scan, celerix::stat::detail::scanned, scanned.size ());
		stats.add (celerix::stat::type::backlog_scan, celerix::stat::detail::activated, activated.size ());

		// Notify about scanned and activated accounts without holding database transaction
		batch_scanned.notify (scanned);
		batch_activated.notify (activated);

		lock.lock ();
	}
}

celerix::container_info celerix::backlog_scan::container_info () const
{
	celerix::lock_guard<celerix::mutex> guard{ mutex };
	celerix::container_info info;
	info.put ("limiter", limiter.size ());
	return info;
}

/*
 * backlog_scan_config
 */

celerix::error celerix::backlog_scan_config::serialize (celerix::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Control if ongoing backlog population is enabled. If not, backlog population can still be triggered by RPC \ntype:bool");
	toml.put ("batch_size", batch_size, "Size of a single batch. Larger batches reduce overhead, but may put more pressure on other node components. \ntype:uint");
	toml.put ("rate_limit", rate_limit, "Number of accounts per second to process when doing backlog population scan. Increasing this value will help unconfirmed frontiers get into election prioritization queue faster. Use 0 to process as fast as possible, but be aware that it may consume a lot of resources. \ntype:uint");

	return toml.get_error ();
}

celerix::error celerix::backlog_scan_config::deserialize (celerix::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("batch_size", batch_size);
	toml.get ("rate_limit", rate_limit);

	return toml.get_error ();
}
