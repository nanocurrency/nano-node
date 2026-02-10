#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/bucketing.hpp>
#include <nano/node/election.hpp>
#include <nano/node/ledger_notifications.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>
#include <nano/store/ledger/confirmation_height.hpp>

nano::scheduler::priority::priority (nano::node_config & node_config, nano::node & node_a, nano::ledger & ledger_a, nano::ledger_notifications & ledger_notifications_a, nano::bucketing & bucketing_a, nano::active_elections & active_a, nano::cementing_set & cementing_set_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ node_config.priority_scheduler },
	node{ node_a },
	ledger{ ledger_a },
	ledger_notifications{ ledger_notifications_a },
	bucketing{ bucketing_a },
	active{ active_a },
	cementing_set{ cementing_set_a },
	stats{ stats_a },
	logger{ logger_a },
	pool{ config.max_blocks, config.reserved_blocks }
{
	for (auto const & index : bucketing.bucket_indices ())
	{
		buckets[index] = std::make_unique<scheduler::bucket> (index, config, active, stats, logger);
	}

	if (!config.enable)
	{
		return;
	}

	// Activate accounts with fresh blocks
	ledger_notifications.blocks_processed.add ([this] (auto const & batch) {
		auto transaction = ledger.tx_begin_read ();
		for (auto const & [result, context] : batch)
		{
			if (result == nano::block_status::progress)
			{
				release_assert (context.block != nullptr);
				activate (transaction, context.block->account ());
			}
		}
	});

	// Activate successors of cemented blocks
	cementing_set.batch_cemented.add ([this] (auto const & batch) {
		if (node.flags.disable_activate_successors)
		{
			return;
		}

		auto transaction = ledger.tx_begin_read ();
		for (auto const & context : batch)
		{
			release_assert (context.block != nullptr);
			activate_successors (transaction, *context.block);
		}
	});
}

nano::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());
}

void nano::scheduler::priority::notify ()
{
	condition.notify_all ();
}

void nano::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_priority);
		run ();
	} };

	cleanup_thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_priority);
		run_cleanup ();
	} };
}

void nano::scheduler::priority::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
	join_or_pass (cleanup_thread);
}

bool nano::scheduler::priority::activate (secure::transaction const & transaction, nano::account const & account)
{
	debug_assert (!account.is_zero ());
	if (auto info = ledger.any.account_get (transaction, account))
	{
		nano::confirmation_height_info conf_info;
		ledger.store.confirmation_height.get (transaction, account, conf_info);
		if (conf_info.height < info->block_count)
		{
			return activate (transaction, account, *info, conf_info);
		}
	}
	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_skip);
	return false; // Not activated
}

bool nano::scheduler::priority::activate (secure::transaction const & transaction, nano::account const & account, nano::account_info const & account_info, nano::confirmation_height_info const & conf_info)
{
	debug_assert (conf_info.frontier != account_info.head);

	auto const hash = conf_info.height == 0 ? account_info.open_block : ledger.any.block_successor (transaction, conf_info.frontier).value_or (0);
	auto const block = ledger.any.block_get (transaction, hash);
	if (!block)
	{
		return false; // Not activated
	}

	if (ledger.dependencies_confirmed (transaction, *block))
	{
		auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *block);
		auto const bucket_index = bucketing.bucket_index (priority_balance);

		bool added = false;
		{
			nano::lock_guard<nano::mutex> guard{ mutex };
			added = pool.push (block, bucket_index, priority_timestamp);
		}
		if (added)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);

			logger.debug (nano::log::type::election_scheduler, "Activated block: {} for account: {} (bucket: {}, priority timestamp: {})",
			block->hash (), account, bucket_index, priority_timestamp);

			logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
			nano::log::arg{ "account", account },
			nano::log::arg{ "block", block },
			nano::log::arg{ "time", account_info.modified },
			nano::log::arg{ "priority_balance", priority_balance },
			nano::log::arg{ "priority_timestamp", priority_timestamp });

			condition.notify_all ();
		}
		else
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_full);
		}

		return true; // Activated
	}

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_failed);
	return false; // Not activated
}

bool nano::scheduler::priority::push (std::shared_ptr<nano::block> const & block, nano::bucket_index bucket, nano::priority_timestamp priority)
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		if (!pool.push (block, bucket, priority))
		{
			return false;
		}
	}
	condition.notify_all ();
	return true;
}

bool nano::scheduler::priority::activate_successors (secure::transaction const & transaction, nano::block const & block)
{
	bool result = activate (transaction, block.account ());

	// Start or vote for the next unconfirmed block in the destination account
	if (block.is_send () && !block.destination ().is_zero () && block.destination () != block.account ())
	{
		result |= activate (transaction, block.destination ());
	}

	return result;
}

bool nano::scheduler::priority::contains (nano::block_hash const & hash) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return pool.contains (hash);
}

std::size_t nano::scheduler::priority::size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return pool.size ();
}

bool nano::scheduler::priority::empty () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return pool.empty ();
}

size_t nano::scheduler::priority::pool_size () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return pool.size ();
}

size_t nano::scheduler::priority::pool_size (nano::bucket_index bucket) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };
	return pool.size (bucket);
}

size_t nano::scheduler::priority::election_count (nano::bucket_index bucket) const
{
	auto it = buckets.find (bucket);
	return it != buckets.end () ? it->second->election_count () : 0;
}

bool nano::scheduler::priority::predicate () const
{
	debug_assert (!mutex.try_lock ());
	auto tops = pool.top_all ();
	return std::any_of (buckets.begin (), buckets.end (), [&tops] (auto const & bucket) {
		return bucket.second->available (find_or_default (tops, bucket.first));
	});
}

void nano::scheduler::priority::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (stopped)
		{
			return;
		}

		stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

		// Get the top blocks for each bucket
		auto tops = pool.top_all ();

		lock.unlock ();

		std::deque<nano::block_hash> activated;

		for (auto const & [index, top] : tops)
		{
			auto const & bucket = buckets.at (index);

			if (bucket->available (top))
			{
				bucket->activate (top);
				activated.push_back (top.block->hash ());
			}
		}

		batch_activated.notify (activated); // Notify without the lock held

		lock.lock ();

		// Erase activated blocks from the pool
		if (!activated.empty ())
		{
			pool.erase_all (activated);
		}
	}
}

void nano::scheduler::priority::run_cleanup ()
{
	// As long as there is work done, keep the cleanup loop busy with shorter sleeps
	bool did_work = false;

	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, did_work ? config.cleanup_interval / 10 : config.cleanup_interval, [this] () {
			return stopped;
		});

		did_work = false;

		if (stopped)
		{
			return;
		}

		stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::cleanup);

		lock.unlock ();

		for (auto const & [index, bucket] : buckets)
		{
			did_work |= bucket->cleanup ();
		}

		lock.lock ();
	}
}

nano::container_info nano::scheduler::priority::container_info () const
{
	auto collect_elections = [&] () {
		nano::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->election_count ());
		}
		return info;
	};

	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.add ("elections", collect_elections ());
	info.add ("pool", pool.container_info ());
	return info;
}

/*
 * priority_config
 */

nano::error nano::scheduler::priority_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable priority scheduler. \nType: bool");
	toml.put ("max_blocks", max_blocks, "Total shared pool size across all buckets. \nType: uint64");
	toml.put ("reserved_blocks", reserved_blocks, "Reserved blocks per bucket. \nType: uint64");
	toml.put ("reserved_elections", reserved_elections, "Guaranteed election slots per bucket. \nType: uint64");
	toml.put ("max_elections", max_elections, "Maximum election slots per bucket when AEC has space. \nType: uint64");
	toml.put ("cleanup_interval", cleanup_interval.count (), "Interval between cleanup runs when idle. \nType: milliseconds");

	return toml.get_error ();
}

nano::error nano::scheduler::priority_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("max_blocks", max_blocks);
	toml.get ("reserved_blocks", reserved_blocks);
	toml.get ("reserved_elections", reserved_elections);
	toml.get ("max_elections", max_elections);
	toml.get_duration ("cleanup_interval", cleanup_interval);

	return toml.get_error ();
}