#include <nano/lib/blocks.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/buckets.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (nano::node & node_a, nano::stats & stats_a) :
	node{ node_a },
	stats{ stats_a },
	buckets{ std::make_unique<scheduler::buckets> () }
{
}

nano::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_priority);
		run ();
	} };
}

void nano::scheduler::priority::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
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

	nano::lock_guard<nano::mutex> lock{ mutex };
	buckets->push (time_priority, block, balance_priority);
	notify ();

	return true; // Activated
}

void nano::scheduler::priority::notify ()
{
	condition.notify_all ();
}

std::size_t nano::scheduler::priority::size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return buckets->size ();
}

bool nano::scheduler::priority::empty_locked () const
{
	return buckets->empty ();
}

bool nano::scheduler::priority::empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return empty_locked ();
}

bool nano::scheduler::priority::predicate () const
{
	return node.active.vacancy () > 0 && !buckets->empty ();
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

			if (predicate ())
			{
				auto block = buckets->top ();
				buckets->pop ();
				lock.unlock ();
				stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority);
				auto result = node.active.insert (block);
				if (result.inserted)
				{
					stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority_success);
				}
				if (result.election != nullptr)
				{
					result.election->transition_active ();
				}
			}
			else
			{
				lock.unlock ();
			}
			notify ();
			lock.lock ();
		}
	}
}

std::unique_ptr<nano::container_info_component> nano::scheduler::priority::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (buckets->collect_container_info ("buckets"));
	return composite;
}
