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
	build_region (uint128_t{ 1 } << 88, uint128_t{ 1 } << 92, 2);
	build_region (uint128_t{ 1 } << 92, uint128_t{ 1 } << 96, 4);
	build_region (uint128_t{ 1 } << 96, uint128_t{ 1 } << 100, 8);
	build_region (uint128_t{ 1 } << 100, uint128_t{ 1 } << 104, 16);
	build_region (uint128_t{ 1 } << 104, uint128_t{ 1 } << 108, 16);
	build_region (uint128_t{ 1 } << 108, uint128_t{ 1 } << 112, 8);
	build_region (uint128_t{ 1 } << 112, uint128_t{ 1 } << 116, 4);
	build_region (uint128_t{ 1 } << 116, uint128_t{ 1 } << 120, 2);
	minimums.push_back (uint128_t{ 1 } << 120);

	node.logger.info (nano::log::type::election_scheduler, "Number of buckets: {}", minimums.size ());

	for (size_t i = 0u, n = minimums.size (); i < n; ++i)
	{
		auto bucket = std::make_unique<scheduler::bucket> (minimums[i], node.active);
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
	auto head = node.ledger.confirmed.account_head (transaction, account);
	if (node.ledger.any.account_head (transaction, account) == head)
	{
		return false;
	}
	auto block = node.ledger.any.block_get (transaction, node.ledger.any.block_successor (transaction, { head.is_zero () ? static_cast<nano::uint256_union> (account) : head, head }).value ());
	if (!node.ledger.dependents_confirmed (transaction, *block))
	{
		return false;
	}
	auto const balance_priority = std::max (block->balance ().number (), node.ledger.confirmed.block_balance (transaction, head).value_or (0).number ());
	auto const time_priority = !head.is_zero () ? node.ledger.confirmed.block_get (transaction, head)->sideband ().timestamp : nano::seconds_since_epoch (); // New accounts get current timestamp i.e. lowest priority

	node.stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
	node.logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
	nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
	nano::log::arg{ "block", block },
	nano::log::arg{ "time", time_priority },
	nano::log::arg{ "priority", balance_priority });

	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		auto & bucket = find_bucket (balance_priority);
		bucket.push (time_priority, block);
	}

	notify ();

	return true; // Activated
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
	// return node.active.vacancy (nano::election_behavior::priority) > 0 && !buckets->empty ();

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

	return **it; // TODO: Revisit this
}

std::unique_ptr<nano::container_info_component> nano::scheduler::priority::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	// composite->add_component (buckets->collect_container_info ("buckets"));
	return composite;
}
