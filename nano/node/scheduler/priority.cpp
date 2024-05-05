#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (nano::node & node_a, nano::stats & stats_a) :
	config{ node_a.config.priority_scheduler },
	node{ node_a },
	stats{ stats_a }
{
	std::vector<nano::uint128_t> minimums;

	auto build_region = [&minimums] (uint128_t const & begin, uint128_t const & end, size_t count) {
		auto width = (end - begin) / count;
		for (auto i = 0; i < count; ++i)
		{
			minimums.push_back (begin + i * width);
		}
	};

	minimums.push_back (uint128_t{ 0 });
	build_region (uint128_t{ 1 } << 79, uint128_t{ 1 } << 88, 1);
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	minimums.push_back (uint128_t{ 1 } << 120);

	node.logger.debug (nano::log::type::election_scheduler, "Number of buckets: {}", minimums.size ());

	for (size_t i = 0u, n = minimums.size (); i < n; ++i)
	{
		auto bucket = std::make_unique<scheduler::bucket> (minimums[i], node.config.priority_bucket, node.active, stats);
		buckets.emplace_back (std::move (bucket));
	}
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

	if (!config.enabled)
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
	auto info = node.ledger.any.account_get (transaction, account);
	if (info)
	{
		nano::confirmation_height_info conf_info;
		node.store.confirmation_height.get (transaction, account, conf_info);
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

	auto hash = conf_info.height == 0 ? account_info.open_block : node.ledger.any.block_successor (transaction, conf_info.frontier).value ();
	auto block = node.ledger.any.block_get (transaction, hash);
	release_assert (block != nullptr);

	if (node.ledger.dependents_confirmed (transaction, *block))
	{
		auto const balance = block->balance ();
		auto const previous_balance = node.ledger.any.block_balance (transaction, conf_info.frontier).value_or (0);
		auto const balance_priority = std::max (balance, previous_balance);

		bool added = false;
		{
			auto & bucket = find_bucket (balance_priority);
			added = bucket.push (account_info.modified, block);
		}
		if (added)
		{
			node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
			node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
			nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
			nano::log::arg{ "block", block },
			nano::log::arg{ "time", account_info.modified },
			nano::log::arg{ "priority", balance_priority });

			notify ();
		}
		else
		{
			node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_full);
		}

		return true; // Activated
	}

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activate_failed);
	return false; // Not activated
}

void nano::scheduler::priority::notify ()
{
	condition.notify_all ();
}

std::size_t nano::scheduler::priority::size () const
{
	return std::accumulate (buckets.begin (), buckets.end (), std::size_t{ 0 }, [] (auto const & sum, auto const & bucket) {
		return sum + bucket->size ();
	});
}

bool nano::scheduler::priority::empty () const
{
	return std::all_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket->empty ();
	});
}

bool nano::scheduler::priority::predicate () const
{
	return std::any_of (buckets.begin (), buckets.end (), [] (auto const & bucket) {
		return bucket->available ();
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

			for (auto & bucket : buckets)
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

			for (auto & bucket : buckets)
			{
				bucket->update ();
			}

			lock.lock ();
		}
	}
}

auto nano::scheduler::priority::find_bucket (nano::uint128_t priority) -> bucket &
{
	auto it = std::upper_bound (buckets.begin (), buckets.end (), priority, [] (nano::uint128_t const & priority, std::unique_ptr<bucket> const & bucket) {
		return priority < bucket->minimum_balance;
	});
	release_assert (it != buckets.begin ()); // There should always be a bucket with a minimum_balance of 0
	it = std::prev (it);
	return **it;
}

nano::container_info nano::scheduler::priority::container_info () const
{
	auto collect_blocks = [&] () {
		nano::container_info info;
		for (auto i = 0; i < buckets.size (); ++i)
		{
			auto const & bucket = buckets[i];
			info.put (std::to_string (i), bucket->size ());
		}
		return info;
	};

	auto collect_elections = [&] () {
		nano::container_info info;
		for (auto i = 0; i < buckets.size (); ++i)
		{
			auto const & bucket = buckets[i];
			info.put (std::to_string (i), bucket->election_count ());
		}
		return info;
	};

	nano::container_info info;
	info.add ("blocks", collect_blocks ());
	info.add ("elections", collect_elections ());
	return info;
}