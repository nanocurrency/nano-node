#include <nano/lib/saturate.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/backlog_scan.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/ledger/account.hpp>
#include <nano/store/ledger/confirmation_height.hpp>

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
		for (auto result = limiter.consume (config.batch_size); !result; result = limiter.consume (config.batch_size))
		{
			condition.wait_for (lock, result.retry_after);
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

			// Both tables are keyed by account, crawl them in lockstep to avoid a random confirmation height lookup per account
			auto account_crawler = ledger.store.account.crawl (transaction, next);
			auto conf_crawler = ledger.store.confirmation_height.crawl (transaction, next);

			for (size_t count = 0; account_crawler && count < config.batch_size; ++account_crawler, ++count, ++total)
			{
				stats.inc (nano::stat::type::backlog_scan, nano::stat::detail::total);

				auto const & [account, account_info] = *account_crawler;

				conf_crawler.skip_to (account);

				nano::confirmation_height_info conf_info{};
				if (conf_crawler && conf_crawler->first == account)
				{
					conf_info = conf_crawler->second;
				}

				activated_info info{ account, account_info, conf_info };

				scanned.push_back (info);
				if (conf_info.height < account_info.block_count)
				{
					activated.push_back (info);
				}

				next = inc_sat (account.number ());
			}

			done = !account_crawler;
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
	info.put ("limiter", limiter.available ());
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
