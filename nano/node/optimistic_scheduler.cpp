#include <nano/lib/stats.hpp>
#include <nano/node/node.hpp>
#include <nano/node/optimistic_scheduler.hpp>

nano::optimistic_scheduler::optimistic_scheduler (optimistic_scheduler_config const & config_a, nano::node & node_a, nano::active_transactions & active_a, nano::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	active{ active_a },
	stats{ stats_a }
{
}

nano::optimistic_scheduler::~optimistic_scheduler ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::optimistic_scheduler::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::optimistic_scheduler);
		run ();
	} };
}

void nano::optimistic_scheduler::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::optimistic_scheduler::notify ()
{
	condition.notify_all ();
}

bool nano::optimistic_scheduler::activate_predicate (const nano::account_info & account_info, const nano::confirmation_height_info & conf_info) const
{
	// Chain with a big enough gap between account frontier and confirmation frontier
	if (account_info.block_count - conf_info.height > config.gap_threshold)
	{
		return true;
	}

	// Account with nothing confirmed yet
	if (conf_info.height == 0)
	{
		return true;
	}

	return false;
}

bool nano::optimistic_scheduler::activate (const nano::account & account, const nano::account_info & account_info, const nano::confirmation_height_info & conf_info)
{
	debug_assert (account_info.block_count >= conf_info.height);

	if (activate_predicate (account_info, conf_info))
	{
		{
			stats.inc (nano::stat::type::optimistic, nano::stat::detail::activated);

			nano::unique_lock<nano::mutex> lock{ mutex };
			candidates.push_back (account);

			// Limit candidates container size
			if (candidates.size () > config.max_size)
			{
				candidates.pop_front ();
			}
		}
		notify ();
		return true; // Activated
	}

	return false; // Not activated
}

bool nano::optimistic_scheduler::predicate () const
{
	debug_assert (!mutex.try_lock ());

	return !candidates.empty () && active.vacancy_optimistic () > 0;
}

void nano::optimistic_scheduler::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		if (predicate ())
		{
			stats.inc (nano::stat::type::optimistic, nano::stat::detail::loop);

			auto candidate = candidates.front ();
			candidates.pop_front ();

			lock.unlock ();
			run_one (candidate);
			lock.lock ();
		}

		condition.wait (lock, [this] () {
			return stopped || predicate ();
		});
	}
}

void nano::optimistic_scheduler::run_one (nano::account candidate)
{
	auto block = node.head_block (candidate);
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (block->hash ()))
		{
			// Try to insert it into AEC
			// We check for AEC vacancy inside our predicate
			auto result = node.active.insert_optimistic (block);

			stats.inc (nano::stat::type::optimistic, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);
		}
	}
}