#include <nano/lib/blocks.hpp>
#include <nano/lib/stats.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/active_elections.hpp>
#include <nano/node/election_behavior.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/optimistic.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/secure/ledger_set_any.hpp>
#include <nano/secure/ledger_set_confirmed.hpp>

nano::scheduler::optimistic::optimistic (optimistic_config const & config_a, nano::node & node_a, nano::ledger & ledger_a, nano::active_elections & active_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	config{ config_a },
	node{ node_a },
	ledger{ ledger_a },
	active{ active_a },
	network_constants{ network_constants_a },
	stats{ stats_a }
{
}

nano::scheduler::optimistic::~optimistic ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::optimistic::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable)
	{
		return;
	}

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_optimistic);
		run ();
	} };
}

void nano::scheduler::optimistic::stop ()
{
	{
		nano::lock_guard<nano::mutex> guard{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	join_or_pass (thread);
}

void nano::scheduler::optimistic::notify ()
{
	// Only wake up the thread if there is space inside AEC for optimistic elections
	if (active.vacancy (nano::election_behavior::optimistic) > 0)
	{
		condition.notify_all ();
	}
}

bool nano::scheduler::optimistic::activate_predicate (const nano::account_info & account_info, const nano::confirmation_height_info & conf_info) const
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

bool nano::scheduler::optimistic::activate (const nano::account & account, const nano::account_info & account_info, const nano::confirmation_height_info & conf_info)
{
	debug_assert (account_info.block_count >= conf_info.height);

	if (!config.enable)
	{
		return false;
	}

	if (activate_predicate (account_info, conf_info))
	{
		nano::lock_guard<nano::mutex> lock{ mutex };

		// Assume gap_threshold for accounts with nothing confirmed
		auto const unconfirmed_height = std::max (account_info.block_count - conf_info.height, config.gap_threshold);

		auto [it, inserted] = candidates.push_back ({ account, unconfirmed_height, std::chrono::steady_clock::now () });

		stats.inc (nano::stat::type::optimistic_scheduler, inserted ? nano::stat::detail::activated : nano::stat::detail::duplicate);

		// Limit candidates container size
		if (candidates.size () > config.max_size)
		{
			// Remove oldest candidate
			candidates.pop_front ();
		}

		// Not notifying the thread immediately here, since we need to wait for activation_delay to elapse

		return inserted;
	}

	return false; // Not activated
}

bool nano::scheduler::optimistic::predicate () const
{
	debug_assert (!mutex.try_lock ());

	// Check if there is space inside AEC for a new optimistic election
	return !candidates.empty () && active.vacancy (nano::election_behavior::optimistic) > 0;
}

auto nano::scheduler::optimistic::snapshot (size_t max_count) const -> std::deque<entry>
{
	auto const now = std::chrono::steady_clock::now ();

	std::deque<entry> result;

	auto & height_index = candidates.get<tag_unconfirmed_height> ();
	for (auto it = height_index.begin (); it != height_index.end () && result.size () < max_count; ++it)
	{
		if (elapsed (it->timestamp, config.activation_delay, now))
		{
			result.push_back (*it);
		}
	}

	return result;
}

void nano::scheduler::optimistic::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		// Ignore predicate in condition, we always want to wait for activation_delay to elapse before next wake up
		condition.wait_for (lock, config.activation_delay, [this] () {
			return stopped.load ();
		});

		if (stopped)
		{
			return;
		}

		if (predicate ())
		{
			stats.inc (nano::stat::type::optimistic_scheduler, nano::stat::detail::loop);

			run_iterative (lock);
			debug_assert (!lock.owns_lock ());
			lock.lock ();
		}
	}
}

void nano::scheduler::optimistic::run_iterative (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	debug_assert (!mutex.try_lock ());

	auto tops = snapshot (active.limit (nano::election_behavior::optimistic));

	lock.unlock ();

	auto transaction = ledger.tx_begin_read ();

	for (auto const & candidate : tops)
	{
		if (stopped)
		{
			return;
		}

		transaction.refresh_if_needed ();

		bool good = run_one (transaction, candidate);
		if (!good)
		{
			// Remove no longer valid candidate
			nano::lock_guard<nano::mutex> guard{ mutex };
			auto & account_index = candidates.get<tag_account> ();
			account_index.erase (candidate.account);
			stats.inc (nano::stat::type::optimistic_scheduler, nano::stat::detail::erased);
		}
	}
}

bool nano::scheduler::optimistic::run_one (secure::transaction const & transaction, entry const & candidate)
{
	auto block = ledger.any.block_get (transaction, ledger.any.account_head (transaction, candidate.account));
	if (block)
	{
		// Ensure block is not already confirmed
		if (!node.block_confirmed_or_being_confirmed (transaction, block->hash ()))
		{
			// Try to insert it into AEC
			auto result = node.active.insert (block, nano::election_behavior::optimistic);
			stats.inc (nano::stat::type::optimistic_scheduler, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);

			return true; // Activation attempted
		}
	}
	return false; // Activation not attempted, block not found or already confirmed, should be erased from candidates
}

nano::container_info nano::scheduler::optimistic::container_info () const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	nano::container_info info;
	info.put ("candidates", candidates);
	return info;
}

/*
 * optimistic_scheduler_config
 */

nano::error nano::scheduler::optimistic_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("enable", enable);
	toml.get ("gap_threshold", gap_threshold);
	toml.get ("max_size", max_size);
	toml.get_duration ("activation_delay", activation_delay);

	return toml.get_error ();
}

nano::error nano::scheduler::optimistic_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable optimistic elections\ntype:bool");
	toml.put ("gap_threshold", gap_threshold, "Minimum difference between confirmation frontier and account frontier to become a candidate for optimistic confirmation\ntype:uint64");
	toml.put ("max_size", max_size, "Maximum number of candidates stored in memory\ntype:uint64");
	toml.put ("activation_delay", activation_delay.count (), "How much to delay activation of optimistic elections to avoid interfering with election scheduler\ntype:milliseconds");

	return toml.get_error ();
}
