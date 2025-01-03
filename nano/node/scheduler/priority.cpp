#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/bucketing.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (nano::node_config & node_config, nano::node & node_a, nano::ledger & ledger_a, nano::bucketing & bucketing_a, nano::block_processor & block_processor_a, nano::active_elections & active_a, nano::confirming_set & confirming_set_a, nano::stats & stats_a, nano::logger & logger_a) :
	config{ node_config.priority_scheduler },
	node{ node_a },
	ledger{ ledger_a },
	bucketing{ bucketing_a },
	block_processor{ block_processor_a },
	active{ active_a },
	confirming_set{ confirming_set_a },
	stats{ stats_a },
	logger{ logger_a }
{
	for (auto const & index : bucketing.bucket_indices ())
	{
		buckets[index] = std::make_unique<scheduler::bucket> (index, node_config.priority_bucket, active, stats);
	}

	// Activate accounts with fresh blocks
	block_processor.batch_processed.add ([this] (auto const & batch) {
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
	confirming_set.batch_cemented.add ([this] (auto const & batch) {
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

	if (ledger.dependents_confirmed (transaction, *block))
	{
		auto const [priority_balance, priority_timestamp] = ledger.block_priority (transaction, *block);
		auto const bucket_index = bucketing.bucket_index (priority_balance);

		bool added = false;
		{
			auto const & bucket = buckets.at (bucket_index);
			release_assert (bucket);
			added = bucket->push (priority_timestamp, block);
		}
		if (added)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
			logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
			nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
			nano::log::arg{ "block", block },
			nano::log::arg{ "time", account_info.modified },
			nano::log::arg{ "priority_balance", priority_balance },
			nano::log::arg{ "priority_timestamp", priority_timestamp });

			notify ();
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
	return std::any_of (buckets.begin (), buckets.end (), [&hash] (auto const & bucket) {
		return bucket.second->contains (hash);
	});
}

void nano::scheduler::priority::notify ()
{
	condition.notify_all ();
}

std::size_t nano::scheduler::priority::size () const
{
	return std::accumulate (buckets.begin (), buckets.end (), std::size_t{ 0 }, [] (auto const & sum, auto const & bucket) {
		return sum + bucket.second->size ();
	});
}

bool nano::scheduler::priority::empty () const
{
	return std::all_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket.second->empty ();
	});
}

bool nano::scheduler::priority::predicate () const
{
	return std::any_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket.second->available ();
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
		if (!stopped)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			lock.unlock ();

			for (auto const & [index, bucket] : buckets)
			{
				if (bucket->available ())
				{
					bucket->activate ();
				}
			}

			lock.lock ();
		}
	}
}

void nano::scheduler::priority::run_cleanup ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s, [this] () {
			return stopped;
		});
		if (!stopped)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::cleanup);

			lock.unlock ();

			for (auto const & [index, bucket] : buckets)
			{
				bucket->update ();
			}

			lock.lock ();
		}
	}
}

nano::container_info nano::scheduler::priority::container_info () const
{
	auto collect_blocks = [&] () {
		nano::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->size ());
		}
		return info;
	};

	auto collect_elections = [&] () {
		nano::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->election_count ());
		}
		return info;
	};

	nano::container_info info;
	info.add ("blocks", collect_blocks ());
	info.add ("elections", collect_elections ());
	return info;
}