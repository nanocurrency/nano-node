#include <celerix/lib/blocks.hpp>
#include <celerix/node/active_elections.hpp>
#include <celerix/node/bucketing.hpp>
#include <celerix/node/election.hpp>
#include <celerix/node/node.hpp>
#include <celerix/node/scheduler/priority.hpp>
#include <celerix/secure/ledger.hpp>
#include <celerix/secure/ledger_set_any.hpp>
#include <celerix/secure/ledger_set_confirmed.hpp>

celerix::scheduler::priority::priority (celerix::node_config & node_config, celerix::node & node_a, celerix::ledger & ledger_a, celerix::bucketing & bucketing_a, celerix::block_processor & block_processor_a, celerix::active_elections & active_a, celerix::confirming_set & confirming_set_a, celerix::stats & stats_a, celerix::logger & logger_a) :
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
			if (result == celerix::block_status::progress)
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

celerix::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());
}

void celerix::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());
	debug_assert (!cleanup_thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::scheduler_priority);
		run ();
	} };

	cleanup_thread = std::thread{ [this] () {
		celerix::thread_role::set (celerix::thread_role::name::scheduler_priority);
		run_cleanup ();
	} };
}

void celerix::scheduler::priority::stop ()
{
	{
		celerix::lock_guard<celerix::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
	join_or_pass (cleanup_thread);
}

bool celerix::scheduler::priority::activate (secure::transaction const & transaction, celerix::account const & account)
{
	debug_assert (!account.is_zero ());
	if (auto info = ledger.any.account_get (transaction, account))
	{
		celerix::confirmation_height_info conf_info;
		ledger.store.confirmation_height.get (transaction, account, conf_info);
		if (conf_info.height < info->block_count)
		{
			return activate (transaction, account, *info, conf_info);
		}
	}
	stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::activate_skip);
	return false; // Not activated
}

bool celerix::scheduler::priority::activate (secure::transaction const & transaction, celerix::account const & account, celerix::account_info const & account_info, celerix::confirmation_height_info const & conf_info)
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
			stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::activated);
			logger.trace (celerix::log::type::election_scheduler, celerix::log::detail::block_activated,
			celerix::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
			celerix::log::arg{ "block", block },
			celerix::log::arg{ "time", account_info.modified },
			celerix::log::arg{ "priority_balance", priority_balance },
			celerix::log::arg{ "priority_timestamp", priority_timestamp });

			notify ();
		}
		else
		{
			stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::activate_full);
		}

		return true; // Activated
	}

	stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::activate_failed);
	return false; // Not activated
}

bool celerix::scheduler::priority::activate_successors (secure::transaction const & transaction, celerix::block const & block)
{
	bool result = activate (transaction, block.account ());
	// Start or vote for the next unconfirmed block in the destination account
	if (block.is_send () && !block.destination ().is_zero () && block.destination () != block.account ())
	{
		result |= activate (transaction, block.destination ());
	}
	return result;
}

bool celerix::scheduler::priority::contains (celerix::block_hash const & hash) const
{
	return std::any_of (buckets.begin (), buckets.end (), [&hash] (auto const & bucket) {
		return bucket.second->contains (hash);
	});
}

void celerix::scheduler::priority::notify ()
{
	condition.notify_all ();
}

std::size_t celerix::scheduler::priority::size () const
{
	return std::accumulate (buckets.begin (), buckets.end (), std::size_t{ 0 }, [] (auto const & sum, auto const & bucket) {
		return sum + bucket.second->size ();
	});
}

bool celerix::scheduler::priority::empty () const
{
	return std::all_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket.second->empty ();
	});
}

bool celerix::scheduler::priority::predicate () const
{
	return std::any_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket.second->available ();
	});
}

void celerix::scheduler::priority::run ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::loop);

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

void celerix::scheduler::priority::run_cleanup ()
{
	celerix::unique_lock<celerix::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait_for (lock, 1s, [this] () {
			return stopped;
		});
		if (!stopped)
		{
			stats.inc (celerix::stat::type::election_scheduler, celerix::stat::detail::cleanup);

			lock.unlock ();

			for (auto const & [index, bucket] : buckets)
			{
				bucket->update ();
			}

			lock.lock ();
		}
	}
}

celerix::container_info celerix::scheduler::priority::container_info () const
{
	auto collect_blocks = [&] () {
		celerix::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->size ());
		}
		return info;
	};

	auto collect_elections = [&] () {
		celerix::container_info info;
		for (auto const & [index, bucket] : buckets)
		{
			info.put (std::to_string (index), bucket->election_count ());
		}
		return info;
	};

	celerix::container_info info;
	info.add ("blocks", collect_blocks ());
	info.add ("elections", collect_elections ());
	return info;
}