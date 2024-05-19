#include <nano/lib/blocks.hpp>
#include <nano/lib/thread_roles.hpp>
#include <nano/lib/threading.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election.hpp>
#include <nano/node/scheduler/bucket.hpp>
#include <nano/node/scheduler/buckets.hpp>
#include <nano/node/scheduler/priority.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::priority::priority (priority_config const & config, nano::ledger & ledger, nano::active_elections & active, nano::stats & stats, nano::logger & logger) :
	config{ config },
	ledger{ ledger },
	active{ active },
	stats{ stats },
	logger{ logger },
	buckets{ std::make_unique<scheduler::buckets> (config.bucket_maximum) }
{
}

nano::scheduler::priority::~priority ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
	debug_assert (tracking.size () == buckets->active ());
}

void nano::scheduler::priority::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enabled)
	{
		return;
	}

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
	auto head = ledger.confirmed.account_head (transaction, account);
	if (ledger.any.account_head (transaction, account) == head)
	{
		return false;
	}
	auto block = ledger.any.block_get (transaction, ledger.any.block_successor (transaction, { head.is_zero () ? static_cast<nano::uint256_union> (account) : head, head }).value ());
	if (!ledger.dependents_confirmed (transaction, *block))
	{
		return false;
	}
	auto const balance_priority = std::max (block->balance ().number (), ledger.confirmed.block_balance (transaction, head).value_or (0).number ());
	auto const time_priority = !head.is_zero () ? ledger.confirmed.block_get (transaction, head)->sideband ().timestamp : nano::seconds_since_epoch (); // New accounts get current timestamp i.e. lowest priority

	stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::activated);
	logger.trace (nano::log::type::election_scheduler, nano::log::detail::block_activated,
	nano::log::arg{ "account", account.to_account () }, // TODO: Convert to lazy eval
	nano::log::arg{ "block", block },
	nano::log::arg{ "time", time_priority },
	nano::log::arg{ "priority", balance_priority });

	nano::lock_guard<nano::mutex> lock{ mutex };
	auto & bucket = buckets->bucket (balance_priority);
	bucket.push (time_priority, block);
	if (bucket.active < bucket.maximum)
	{
		notify ();
	}
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

void nano::scheduler::priority::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock);
		if (!stopped)
		{
			stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::loop);

			while (auto bucket = buckets->next ())
			{
				debug_assert (!bucket->empty () && bucket->active < bucket->maximum);
				auto block = bucket->top ();
				debug_assert (block != nullptr);
				bucket->pop ();
				if (tracking.find (block->qualified_root ()) != tracking.end ())
				{
					continue;
				}
				// Increment counter and start tracking for block's qualified root
				// Start tracking before we actually attempt to start election since election cleanup happens asynchronously
				++bucket->active;
				[[maybe_unused]] auto inserted = tracking.emplace (block->qualified_root (), bucket);
				debug_assert (inserted.second);
				nano::election_insertion_result result;
				{
					// Do slow operations outside lock

					lock.unlock ();
					stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority);
					result = active.insert (block);
					if (result.inserted)
					{
						stats.inc (nano::stat::type::election_scheduler, nano::stat::detail::insert_priority_success);
					}
					if (result.election != nullptr)
					{
						result.election->transition_active ();
					}
					lock.lock ();
				}
				if (!result.election)
				{
					// No election exists or was created so clean up dangling tracking information
					--bucket->active;
					[[maybe_unused]] auto erased = tracking.erase (block->qualified_root ());
					debug_assert (erased == 1);
				}
			}
		}
	}
}

void nano::scheduler::priority::election_stopped (std::shared_ptr<nano::election> election)
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	if (auto existing = tracking.find (election->qualified_root); existing != tracking.end ())
	{
		auto & bucket = *existing->second;
		--bucket.active;
		if (!bucket.empty ())
		{
			notify ();
		}
		// Clean up election stop event subscription
		tracking.erase (existing);
	}
}

std::unique_ptr<nano::container_info_component> nano::scheduler::priority::collect_container_info (std::string const & name)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (buckets->collect_container_info ("buckets"));
	return composite;
}
